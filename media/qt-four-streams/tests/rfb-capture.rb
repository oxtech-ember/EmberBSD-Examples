#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD contributors (AI-assisted).
require 'socket'
require 'json'
require 'digest'
require 'time'

# Only observed, fully covered Raw updates become PPM files. This client sends
# no input events and accepts only the None security type on a loopback socket.
class CaptureDeadline < StandardError; end

class RfbCapture
  MAX_BYTES = 64 * 1024 * 1024
  REQUEST_INTERVAL_MS = 1000
  attr_reader :received, :sent, :partial_read

  def initialize(socket)
    @socket = socket
    @received = @sent = 0
    @partial_read = 0
  end

  def now
    Process.clock_gettime(Process::CLOCK_MONOTONIC)
  end

  def read_exact(count, deadline)
    raise 'Invalid read size' unless (0..MAX_BYTES).cover?(count)
    result = String.new(encoding: Encoding::BINARY, capacity: count)
    @partial_read = 0
    while result.bytesize < count
      remaining = deadline - now
      raise CaptureDeadline, 'Read deadline' unless remaining.positive?
      value = @socket.read_nonblock(count - result.bytesize, exception: false)
      case value
      when :wait_readable
        raise CaptureDeadline, 'Read deadline' unless IO.select([@socket], nil, nil, remaining)
      when nil
        raise EOFError, "Server closed during read (#{result.bytesize}/#{count} bytes)"
      else
        result << value
        @received += value.bytesize
        @partial_read = result.bytesize
      end
    end
    @partial_read = 0
    result
  end

  def write_all(bytes, deadline)
    offset = 0
    while offset < bytes.bytesize
      remaining = deadline - now
      raise CaptureDeadline, 'Write deadline' unless remaining.positive?
      count = @socket.write_nonblock(bytes.byteslice(offset..), exception: false)
      if count == :wait_writable
        raise CaptureDeadline, 'Write deadline' unless IO.select(nil, [@socket], nil, remaining)
      else
        raise 'Zero-length socket write' unless count.positive?
        offset += count
        @sent += count
      end
    end
  end

  def string(deadline)
    length = read_exact(4, deadline).unpack1('N')
    raise 'Server string too large' if length > 65_536
    read_exact(length, deadline).force_encoding(Encoding::UTF_8).scrub
  end
end

def save_report(directory, report)
  temporary = directory + '/capture.json.tmp'
  File.write(temporary, JSON.pretty_generate(report) + "\n")
  File.rename(temporary, directory + '/capture.json')
end

unless (3..4).cover?(ARGV.length)
  warn 'Usage: rfb-capture.rb HOST PORT NEW_DIR [DURATION_MS=6000]'
  exit 2
end

directory = nil
report = nil
socket = nil
client = nil
started = Process.clock_gettime(Process::CLOCK_MONOTONIC)
begin
  host, port_text, output, duration_text = ARGV
  raise 'Only localhost, 127.0.0.1 or ::1 is accepted' unless %w[localhost 127.0.0.1 ::1].include?(host)
  raise 'Invalid port' unless port_text.match?(/\A[0-9]{1,5}\z/) && (1..65_535).cover?(port_text.to_i)
  duration_text ||= '6000'
  raise 'Duration must be 1..55000 milliseconds' unless duration_text.match?(/\A[0-9]+\z/) && (1..55_000).cover?(duration_text.to_i)
  directory = File.expand_path(output)
  Dir.mkdir(directory, 0o755)
  report = { schema: 'emberbsd.qt-four-streams.rfb.v1', status: 'connecting',
             created_utc: Time.now.utc.iso8601, host: host, port: port_text.to_i,
             requested_duration_ms: duration_text.to_i, frames: [], updates: [],
             requested_interval_ms: RfbCapture::REQUEST_INTERVAL_MS,
             note: 'RFB observations are not decoder or presentation FPS measurements.',
             ppm_storage_budget_bytes: RfbCapture::MAX_BYTES }
  save_report(directory, report)
  connect_deadline = started + 5
  report[:connection_attempts] = 0
  loop do
    report[:connection_attempts] += 1
    remaining = connect_deadline - Process.clock_gettime(Process::CLOCK_MONOTONIC)
    raise CaptureDeadline, 'Connect deadline' unless remaining.positive?
    begin
      socket = Socket.tcp(host == 'localhost' ? '127.0.0.1' : host, port_text.to_i,
                          connect_timeout: [remaining, 1].min)
      break
    rescue Errno::ECONNREFUSED
      sleep([remaining, 0.05].min)
    end
  end
  client = RfbCapture.new(socket)
  handshake_deadline = [started + 10, started + 60].min
  banner = client.read_exact(12, handshake_deadline)
  raise 'Invalid RFB banner' unless banner.match?(/\ARFB 003\.(003|007|008)\n\z/)
  version = banner.byteslice(8, 3).to_i
  negotiated = version >= 8 ? 8 : (version >= 7 ? 7 : 3)
  report[:server_banner] = banner
  report[:negotiated_protocol] = "3.#{negotiated}"
  client.write_all(format("RFB 003.%03d\n", negotiated), handshake_deadline)
  if negotiated == 3
    security = client.read_exact(4, handshake_deadline).unpack1('N')
    raise 'Server refused security: ' + client.string(handshake_deadline) if security.zero?
    raise 'Only None security is supported' unless security == 1
  else
    count = client.read_exact(1, handshake_deadline).unpack1('C')
    raise 'Server refused security: ' + client.string(handshake_deadline) if count.zero?
    types = client.read_exact(count, handshake_deadline).bytes
    report[:offered_security_types] = types
    raise 'Server does not offer None security' unless types.include?(1)
    client.write_all([1].pack('C'), handshake_deadline)
    if negotiated >= 8
      result = client.read_exact(4, handshake_deadline).unpack1('N')
      raise 'None security failed: ' + client.string(handshake_deadline) unless result.zero?
    end
  end
  client.write_all([1].pack('C'), handshake_deadline) # shared session
  width, height = client.read_exact(4, handshake_deadline).unpack('n2')
  raise 'Invalid framebuffer dimensions' unless width.positive? && height.positive? && width * height * 4 <= RfbCapture::MAX_BYTES
  server_format = client.read_exact(16, handshake_deadline).unpack('C4n3C3x3')
  report[:server_pixel_format] = %w[bits_per_pixel depth big_endian true_color red_max green_max blue_max red_shift green_shift blue_shift].zip(server_format).to_h
  report[:server_name] = client.string(handshake_deadline)
  report[:width] = width
  report[:height] = height
  requested = [32, 24, 0, 1, 255, 255, 255, 16, 8, 0].pack('C4n3C3x3')
  client.write_all([0, 0, 0, 0].pack('C4') + requested, handshake_deadline)
  client.write_all([2, 0, 1, 0].pack('CCnl>'), handshake_deadline) # Raw encoding only
  report[:requested_pixel_format] = '32-bit little-endian true-color RGB888, shifts 16/8/0'
  report[:encoding] = 'Raw (0)'
  report[:status] = 'observing'
  observation_started = client.now
  deadline = [observation_started + duration_text.to_i / 1000.0, started + 60].min
  report[:observation_start_monotonic_seconds] = observation_started
  stored_bytes = 0
  next_request = observation_started
  messages = 0
  begin
    while client.now < deadline
      wait = [next_request - client.now, deadline - client.now].min
      IO.select(nil, nil, nil, wait) if wait.positive?
      break if client.now >= deadline
      request_started = client.now
      client.write_all([3, 0, 0, 0, width, height].pack('CCn4'), deadline)
      update = nil
      until update
        messages += 1
        raise 'Too many server messages' if messages > 4096
        type = client.read_exact(1, deadline).unpack1('C')
        case type
        when 0
          rectangles = client.read_exact(3, deadline).unpack('Cn').last
          raise 'Too many update rectangles' if rectangles > 4096
          rgb = "\0".b * (width * height * 3)
          coverage = "\0".b * (width * height)
          rectangle_records = []
          raw_bytes = 0
          rectangles.times do
            x, y, w, h, encoding = client.read_exact(12, deadline).unpack('n4l>')
            raise 'Unexpected encoding; only Raw was requested' unless encoding.zero?
            raise 'Invalid rectangle bounds' unless w.positive? && h.positive? && x + w <= width && y + h <= height
            bytes = w * h * 4
            raw_bytes += bytes
            raise 'Update exceeds byte budget' if raw_bytes > RfbCapture::MAX_BYTES
            raw = client.read_exact(bytes, deadline)
            h.times do |row|
              raise CaptureDeadline, 'Pixel conversion deadline' if client.now >= deadline
              row_rgb = String.new(encoding: Encoding::BINARY, capacity: w * 3)
              raw.byteslice(row * w * 4, w * 4).unpack('V*').each do |pixel|
                row_rgb << ((pixel >> 16) & 255) << ((pixel >> 8) & 255) << (pixel & 255)
              end
              rgb[((y + row) * width + x) * 3, w * 3] = row_rgb
              coverage[(y + row) * width + x, w] = "\1".b * w
            end
            rectangle_records << [x, y, w, h]
          end
          update = { rectangles: rectangle_records, raw_bytes: raw_bytes,
                     full_coverage: coverage.count("\1") == width * height,
                     monotonic_seconds: client.now }
          if update[:full_coverage]
            digest = Digest::SHA256.hexdigest(rgb)
            frame = { index: report[:frames].length, monotonic_seconds: client.now,
                      rgb_sha256: digest, rgb_bytes: rgb.bytesize, width: width, height: height }
            ppm = "P6\n#{width} #{height}\n255\n".b + rgb
            if stored_bytes + ppm.bytesize <= RfbCapture::MAX_BYTES
              frame[:file] = format('frame-%04d.ppm', frame[:index])
              File.binwrite(directory + '/' + frame[:file], ppm)
              frame[:ppm_sha256] = Digest::SHA256.hexdigest(ppm)
              stored_bytes += ppm.bytesize
            else
              frame[:file] = nil
              frame[:storage_note] = 'PPM storage budget reached; only real framebuffer hash retained'
            end
            report[:frames] << frame
          end
          report[:updates] << update
          save_report(directory, report)
        when 1
          _padding, first, count = client.read_exact(5, deadline).unpack('Cnn')
          raise 'Color-map message too large' if first + count > 65_536
          client.read_exact(count * 6, deadline)
        when 2
          report[:bell_messages] = report.fetch(:bell_messages, 0) + 1
        when 3
          client.read_exact(3, deadline)
          client.string(deadline)
          report[:cut_text_messages] = report.fetch(:cut_text_messages, 0) + 1
        else
          raise "Unsupported server message #{type}"
        end
      end
      next_request = [request_started + RfbCapture::REQUEST_INTERVAL_MS / 1000.0, client.now].max
    end
    report[:status] = 'complete'
  rescue CaptureDeadline => error
    raise if client.now < deadline - 0.01
    report[:status] = 'observation-deadline'
    report[:terminal_detail] = error.message
    report[:unfinished_read_bytes] = client.partial_read
  rescue EOFError, Errno::ECONNRESET, Errno::EPIPE => error
    report[:status] = 'closed'
    report[:terminal_detail] = "#{error.class}: #{error.message}"
    report[:complete_observation] = false
    report[:unfinished_read_bytes] = client.partial_read
  end
  report[:observation_elapsed_ms] = ((client.now - observation_started) * 1000).round(3)
  report[:complete_observation] = report[:status] != 'closed'
  stored = report[:frames].select { |frame| frame[:file] }
  report[:stored_ppm_bytes] = stored_bytes
  report[:stored_frames] = stored.length
  report[:distinct_stored_frames] = stored.map { |frame| frame[:rgb_sha256] }.uniq.length
  report[:evidence_ok] = stored.length >= 2 && report[:distinct_stored_frames] >= 2
  raise 'Need at least two nonidentical observed full-frame snapshots' unless report[:evidence_ok]
rescue StandardError => error
  if report
    report[:status_before_error] = report[:status]
    report[:status] = 'failed'
    report[:error] = "#{error.class}: #{error.message}"
  end
  warn "RFB capture failed: #{error.class}: #{error.message}"
ensure
  socket.close if socket && !socket.closed?
  if report
    report[:received_bytes] = client&.received
    report[:sent_bytes] = client&.sent
    report[:whole_elapsed_ms] = ((Process.clock_gettime(Process::CLOCK_MONOTONIC) - started) * 1000).round(3)
    save_report(directory, report)
    puts JSON.generate(report.reject { |key, _| %i[frames updates].include?(key) })
  end
end
exit(report && report[:evidence_ok] && report[:status] != 'failed' ? 0 : 1)
