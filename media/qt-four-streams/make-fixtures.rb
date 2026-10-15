#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD contributors (AI-assisted).
require 'digest'
require 'json'
require 'open3'
require 'time'

def usage
  warn 'Usage: make-fixtures.rb NATIVE_FFMPEG NATIVE_GENERATOR NEW_DIR [WIDTHxHEIGHT:FRAMES:FPS]'
  exit 2
end

usage unless (3..4).cover?(ARGV.length)
match = /\A([1-9][0-9]*)x([1-9][0-9]*):([1-9][0-9]*):([1-9][0-9]*)\z/.match(ARGV[3] || '2048x1500:36:9')
usage unless match
width, height, frames, fps = match.captures.map(&:to_i)
usage unless [width, height].all? { |value| (16..32_768).cover?(value) && value.even? } \
             && (2..256).cover?(frames) && (1..240).cover?(fps)

def executable(path)
  resolved = File.realpath(path)
  raise "Not an executable regular file: #{path}" unless File.file?(resolved) && File.executable?(resolved)
  resolved
end

def write_json(path, value)
  temporary = "#{path}.tmp"
  File.write(temporary, "#{JSON.pretty_generate(value)}\n")
  File.rename(temporary, path)
end

def status_json(status)
  { 'success' => status.success?, 'exit_code' => status.exitstatus,
    'term_signal' => status.termsig }
end

def write_all(destination, bytes)
  offset = 0
  while offset < bytes.bytesize
    written = destination.write(bytes.byteslice(offset, bytes.bytesize - offset))
    raise 'Zero-length write to FFmpeg' unless written.positive?
    offset += written
  end
end

output = nil
manifest = nil
begin
  ffmpeg = executable(ARGV[0])
  generator = executable(ARGV[1])
  requested_output = File.expand_path(ARGV[2])
  # mkdir refuses existing files, directories and dangling symlinks atomically.
  Dir.mkdir(requested_output, 0o755)
  output = requested_output
  version_command = [ffmpeg, '-version']
  version_stdout, version_stderr, version_status = Open3.capture3(*version_command)
  File.write(File.join(output, 'ffmpeg-version.txt'), version_stdout + version_stderr)
  raise 'Native FFmpeg -version failed' unless version_status.success?
  expected_bytes = width * height * 3 / 2 * frames
  manifest = {
    'schema' => 'emberbsd.qt-four-streams.fixtures.v1', 'status' => 'running',
    'created_utc' => Time.now.utc.iso8601, 'width' => width, 'height' => height,
    'frames' => frames, 'fps' => fps, 'raw_pixel_format' => 'yuv420p',
    'raw_bytes_per_channel' => expected_bytes,
    'source_time_base' => "1/#{fps}", 'mp4_video_track_time_base' => '1/90000',
    'formula' => 'Y(x,y)=bit(f,x/2)?235:16 for x<16&&y<16; otherwise16+((x/64+3*(y/64)+5*f+41*c)%220);U=V=128;integer division',
    'ffmpeg' => { 'path' => ffmpeg, 'sha256' => Digest::SHA256.file(ffmpeg).hexdigest,
                  'version_command' => version_command, 'version_status' => status_json(version_status),
                  'version_text' => version_stdout + version_stderr },
    'generator' => { 'path' => generator, 'sha256' => Digest::SHA256.file(generator).hexdigest },
    'script_sha256' => Digest::SHA256.file(__FILE__).hexdigest,
    'channels' => []
  }
  manifest_path = File.join(output, 'fixtures.json')
  write_json(manifest_path, manifest)
  4.times do |channel|
    destination = File.join(output, "channel#{channel}.mp4")
    generator_command = [generator, width.to_s, height.to_s, frames.to_s, channel.to_s]
    encoder_command = [
      ffmpeg, '-hide_banner', '-nostdin', '-loglevel', 'info', '-n',
      '-f', 'rawvideo', '-pixel_format', 'yuv420p', '-video_size', "#{width}x#{height}",
      '-framerate', fps.to_s, '-color_range', 'tv', '-colorspace', 'bt709',
      '-color_primaries', 'bt709', '-color_trc', 'bt709', '-i', 'pipe:0',
      '-map', '0:v:0', '-an', '-c:v', 'libx265', '-preset', 'medium',
      '-x265-params', 'lossless=1:keyint=9:min-keyint=9:scenecut=0:bframes=0:pools=1:frame-threads=1',
      '-pix_fmt', 'yuv420p', '-color_range', 'tv', '-colorspace', 'bt709',
      '-color_primaries', 'bt709', '-color_trc', 'bt709', '-tag:v', 'hvc1',
      '-video_track_timescale', '90000', '-movflags', '+faststart',
      '-map_metadata', '-1', '-f', 'mp4', destination
    ]
    record = { 'channel' => channel, 'file' => File.basename(destination),
               'generator_command' => generator_command, 'encoder_command' => encoder_command }
    manifest['channels'] << record
    write_json(manifest_path, manifest)
    digest = Digest::SHA256.new
    raw_bytes = 0
    generator_pid = nil
    encoder_pid = nil
    generator_read, generator_write = IO.pipe
    encoder_read, encoder_write = IO.pipe
    transfer_error = nil
    begin
      File.open(File.join(output, "channel#{channel}-generator.log"), 'wb') do |generator_log|
        File.open(File.join(output, "channel#{channel}-ffmpeg.log"), 'wb') do |encoder_log|
          generator_pid = Process.spawn(*generator_command, out: generator_write, err: generator_log)
          encoder_pid = Process.spawn(*encoder_command, in: encoder_read, out: encoder_log, err: encoder_log)
        end
      end
      generator_write.close
      encoder_read.close
      generator_read.binmode
      encoder_write.binmode
      begin
        loop do
          chunk = generator_read.readpartial(65_536)
          digest.update(chunk)
          raw_bytes += chunk.bytesize
          write_all(encoder_write, chunk)
        end
      rescue EOFError
        # The generator reached EOF. Its actual exit status is checked below.
      end
    rescue StandardError => error
      transfer_error = "#{error.class}: #{error.message}"
    ensure
      [generator_read, generator_write, encoder_read, encoder_write].each do |pipe|
        pipe.close unless pipe.closed?
      end
      record['generator_status'] = status_json(Process.wait2(generator_pid)[1]) if generator_pid
      record['encoder_status'] = status_json(Process.wait2(encoder_pid)[1]) if encoder_pid
    end
    record['raw_bytes'] = raw_bytes
    record['raw_sha256'] = digest.hexdigest
    record['transfer_error'] = transfer_error if transfer_error
    if File.file?(destination)
      record['encoded_bytes'] = File.size(destination)
      record['encoded_sha256'] = Digest::SHA256.file(destination).hexdigest
    end
    write_json(manifest_path, manifest)
    raise "Channel #{channel} transfer failed: #{transfer_error}" if transfer_error
    raise "Channel #{channel} generator failed" unless record.dig('generator_status', 'success')
    raise "Channel #{channel} FFmpeg failed" unless record.dig('encoder_status', 'success')
    raise "Channel #{channel} raw byte count differs" unless raw_bytes == expected_bytes
    raise "Channel #{channel} encoded file is empty" unless record.fetch('encoded_bytes', 0).positive?
  end
  manifest['status'] = 'complete'
  write_json(manifest_path, manifest)
  puts manifest_path
rescue StandardError => error
  if output && manifest
    manifest['status'] = 'failed'
    manifest['error'] = "#{error.class}: #{error.message}"
    write_json(File.join(output, 'fixtures.json'), manifest)
  end
  warn "Fixture generation failed: #{error.class}: #{error.message}"
  exit 1
end
