#!/usr/bin/env ruby
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 EmberBSD contributors.

# Independent RFC 3550 / RFC 7798 parser. No FFmpeg or GStreamer bindings.
require 'digest'
require 'json'
require 'optparse'

MAX_RECORDS = 1_000_000
MAX_CAPTURE_BYTES = 536_870_912
MAX_NAL_BYTES = 16_777_216
MAX_ANNEX_B_BYTES = 268_435_456

class ContractError < StandardError; end

def require_contract(condition, message)
  raise ContractError, message unless condition
end

def capture_records(path, direction)
  require_contract(File.file?(path), "not a regular file: #{path}")
  require_contract(File.size(path) <= MAX_CAPTURE_BYTES, 'capture exceeds byte limit')
  File.open(path, 'rb') do |io|
    require_contract(io.read(8) == 'H265RTP1', 'invalid capture magic')
    count = 0
    last_time = 0
    last_ordinal = 0
    while (header = io.read(24))
      require_contract(header.bytesize == 24, 'truncated capture record header')
      length, ordinal, flags, reserved, elapsed = header.unpack('NNNNQ>')
      count += 1
      require_contract(count <= MAX_RECORDS, 'capture exceeds record limit')
      require_contract(length <= 65_535, 'capture packet exceeds UDP bound')
      require_contract(ordinal.between?(1, MAX_RECORDS), 'invalid capture ordinal')
      require_contract(reserved.zero? && (flags & ~255).zero?, 'invalid capture flags')
      require_contract((flags & 3) == direction, 'wrong capture direction')
      require_contract(elapsed >= last_time, 'capture time moved backwards')
      if direction == 1
        require_contract(ordinal == last_ordinal + 1, 'received ordinals are not consecutive')
        require_contract((flags & (8 | 32)).zero?, 'transmit flag in receive capture')
      else
        require_contract((flags & (4 | 16 | 64 | 128)).zero?, 'receive flag in transmit capture')
      end
      packet = io.read(length)
      require_contract(packet && packet.bytesize == length, 'truncated capture datagram')
      last_time = elapsed
      last_ordinal = ordinal
      yield packet, ordinal, flags, elapsed
    end
  end
end

def rtp_packet(packet)
  require_contract(packet.bytesize >= 12, 'RTP header is truncated')
  first, second, sequence, timestamp, ssrc = packet.unpack('CCnNN')
  require_contract(first >> 6 == 2, 'RTP version is not 2')
  offset = 12 + 4 * (first & 15)
  require_contract(offset <= packet.bytesize, 'RTP CSRC list is truncated')
  if (first & 16) != 0
    require_contract(packet.bytesize - offset >= 4, 'RTP extension header is truncated')
    extension = 4 + 4 * packet.byteslice(offset + 2, 2).unpack1('n')
    require_contract(extension <= packet.bytesize - offset, 'RTP extension data is truncated')
    offset += extension
  end
  length = packet.bytesize - offset
  if (first & 32) != 0
    padding = packet.getbyte(-1)
    require_contract(padding.positive? && padding <= length, 'invalid RTP padding')
    length -= padding
  end
  require_contract(length >= 2, 'HEVC payload header is truncated')
  payload = packet.byteslice(offset, length)
  validate_nal_header(payload)
  {sequence: sequence, timestamp: timestamp, ssrc: ssrc,
   pt: second & 127, marker: (second & 128) != 0, payload: payload}
end

def validate_nal_header(nal)
  require_contract(nal.bytesize >= 2, 'HEVC NAL header is truncated')
  require_contract((nal.getbyte(0) & 128).zero?, 'HEVC forbidden_zero_bit is set')
  require_contract((nal.getbyte(1) & 7).positive?, 'HEVC temporal_id_plus1 is zero')
end

options = {pt: 96, min_packets: 1, min_nals: 1, direction: 2, loss: false}
parser = OptionParser.new do |p|
  p.banner = 'Usage: check-rtp.rb CAPTURE [options]'
  p.on('--expect-pt N', Integer) { |n| options[:pt] = n }
  p.on('--expect-ssrc N', Integer) { |n| options[:ssrc] = n }
  p.on('--min-packets N', Integer) { |n| options[:min_packets] = n }
  p.on('--min-nals N', Integer) { |n| options[:min_nals] = n }
  p.on('--clock-step N', Integer) { |n| options[:clock_step] = n }
  p.on('--events DIRECTION', %w[received forwarded]) do |s|
    options[:direction] = s == 'received' ? 1 : 2
  end
  p.on('--allow-loss') { options[:loss] = true }
  p.on('--nal-out FILE') { |s| options[:nal_out] = s }
  p.on('--compare-received FILE') { |s| options[:compare] = s }
end

nal_io = nil
begin
  parser.parse!
  require_contract(ARGV.length == 1, parser.banner)
  require_contract(options[:pt].between?(0, 127), 'payload type is outside 0..127')
  require_contract(!options.key?(:ssrc) || options[:ssrc].between?(0, 0xffff_ffff),
                   'SSRC is outside uint32')
  require_contract(options[:min_packets].between?(0, MAX_RECORDS), 'invalid minimum packet count')
  require_contract(options[:min_nals].between?(0, MAX_RECORDS), 'invalid minimum NAL count')
  require_contract(!options[:clock_step] || options[:clock_step].between?(1, 0x7fff_ffff),
                   'invalid RTP clock step')
  require_contract(!options[:compare] || options[:direction] == 2,
                   'received comparison requires forwarded events')
  if options[:nal_out]
    require_contract(File.expand_path(options[:nal_out]) != File.expand_path(ARGV[0]) &&
                     (!options[:compare] || File.expand_path(options[:nal_out]) !=
                     File.expand_path(options[:compare])), 'NAL output overwrites a capture')
    [ARGV[0], options[:compare]].compact.each do |capture|
      require_contract(!File.exist?(options[:nal_out]) ||
                       !File.identical?(options[:nal_out], capture), 'NAL output aliases a capture')
    end
    nal_io = File.open(options[:nal_out], 'wb')
  end

  received = {}
  if options[:compare]
    capture_records(options[:compare], 1) do |packet, ordinal, flags, _|
      received[ordinal] = [Digest::SHA256.digest(packet), flags, 0]
    end
  end
  summary = {format: 'H265RTP1', packet_count: 0, payload_type: options[:pt],
             duplicate_packets: 0, reordered_packets: 0, sequence_gaps: 0,
             marker_packets: 0, access_units: 0, timestamp_changes_without_marker: 0,
             single_packets: 0, aggregation_packets: 0, fragmentation_packets: 0,
             middle_fu_packets: 0, middle_vcl_fu_packets: 0, middle_fu_types: Hash.new(0),
             complete_nals: 0, incomplete_nals: 0,
             orphan_fragments: 0, nal_types: Hash.new(0), annex_b_bytes: 0}
  stream_ssrc = options[:ssrc]
  highest_sequence = nil
  seen_sequence = {}
  fu = nil
  previous_timestamp = nil
  previous_marker = true
  previous_marker_timestamp = nil
  previous_marker_sequence = nil
  timestamp_seen = {}
  nal_digest = Digest::SHA256.new
  last_tx_ordinal = 0
  comparison_reorders = 0

  emit_nal = lambda do |nal|
    validate_nal_header(nal)
    require_contract(nal.bytesize <= MAX_NAL_BYTES, 'NAL exceeds byte limit')
    type = (nal.getbyte(0) >> 1) & 63
    require_contract(type <= 47, 'nested HEVC packetization unit')
    summary[:complete_nals] += 1
    summary[:nal_types][type] += 1
    bytes = "\x00\x00\x00\x01".b + nal
    summary[:annex_b_bytes] += bytes.bytesize
    require_contract(summary[:annex_b_bytes] <= MAX_ANNEX_B_BYTES, 'Annex B exceeds byte limit')
    nal_digest.update(bytes)
    nal_io.write(bytes) if nal_io
  end

  capture_records(ARGV[0], options[:direction]) do |packet, ordinal, flags, _|
    if options[:compare]
      original = received[ordinal]
      require_contract(original, "forwarded ordinal #{ordinal} was not received")
      require_contract(original[0] == Digest::SHA256.digest(packet),
                       "forwarded ordinal #{ordinal} changed bytes")
      require_contract((original[1] & 4).zero?, "dropped ordinal #{ordinal} was forwarded")
      original[2] += 1
      require_contract(original[2] <= 2, "ordinal #{ordinal} was forwarded more than twice")
      require_contract((original[2] == 2) == ((flags & 8) != 0),
                       "duplicate flag mismatch for ordinal #{ordinal}")
      comparison_reorders += 1 if ordinal < last_tx_ordinal
      last_tx_ordinal = [last_tx_ordinal, ordinal].max
    end
    rtp = rtp_packet(packet)
    summary[:packet_count] += 1
    require_contract(rtp[:pt] == options[:pt], "unexpected RTP payload type #{rtp[:pt]}")
    stream_ssrc ||= rtp[:ssrc]
    require_contract(rtp[:ssrc] == stream_ssrc, 'RTP SSRC changed within capture')
    sequence = rtp[:sequence]
    if highest_sequence
      delta = (sequence - (highest_sequence & 0xffff)) & 0xffff
      delta -= 65_536 if delta >= 32_768
      extended = highest_sequence + delta
    else
      extended = sequence
    end
    if seen_sequence.key?(extended)
      require_contract(seen_sequence[extended] == Digest::SHA256.digest(packet),
                       'duplicate sequence has different RTP bytes')
      summary[:duplicate_packets] += 1
      next
    end
    seen_sequence[extended] = Digest::SHA256.digest(packet)
    if highest_sequence && extended < highest_sequence
      summary[:reordered_packets] += 1
    elsif highest_sequence && extended > highest_sequence + 1
      summary[:sequence_gaps] += extended - highest_sequence - 1
    end
    highest_sequence = [highest_sequence || extended, extended].max
    seen_sequence.delete_if { |key, _| key < highest_sequence - 4096 } if seen_sequence.size > 8192

    if previous_timestamp && rtp[:timestamp] != previous_timestamp && !previous_marker
      summary[:timestamp_changes_without_marker] += 1
    end
    if rtp[:marker]
      summary[:marker_packets] += 1
      require_contract(!timestamp_seen.key?(rtp[:timestamp]), 'multiple RTP markers for one timestamp')
      timestamp_seen[rtp[:timestamp]] = true
      if previous_marker_timestamp
        advance = if extended > previous_marker_sequence
                    (rtp[:timestamp] - previous_marker_timestamp) & 0xffff_ffff
                  else
                    (previous_marker_timestamp - rtp[:timestamp]) & 0xffff_ffff
                  end
        require_contract(advance.between?(1, 0x7fff_ffff), 'RTP marker timestamp did not advance')
        if options[:clock_step]
          valid_step = options[:loss] ? (advance % options[:clock_step]).zero? :
            advance == options[:clock_step]
          require_contract(valid_step, 'unexpected RTP clock step')
        end
      end
      if previous_marker_sequence.nil? || extended > previous_marker_sequence
        previous_marker_timestamp = rtp[:timestamp]
        previous_marker_sequence = extended
      end
      summary[:access_units] += 1
    end
    previous_timestamp = rtp[:timestamp]
    previous_marker = rtp[:marker]
    payload = rtp[:payload]
    type = (payload.getbyte(0) >> 1) & 63
    if type <= 47
      if fu
        summary[:incomplete_nals] += 1
        fu = nil
      end
      summary[:single_packets] += 1
      emit_nal.call(payload)
    elsif type == 48
      if fu
        summary[:incomplete_nals] += 1
        fu = nil
      end
      summary[:aggregation_packets] += 1
      offset = 2
      count = 0
      while offset < payload.bytesize
        require_contract(payload.bytesize - offset >= 2, 'AP length is truncated')
        length = payload.byteslice(offset, 2).unpack1('n')
        offset += 2
        require_contract(length >= 2 && length <= payload.bytesize - offset, 'invalid AP NAL length')
        emit_nal.call(payload.byteslice(offset, length))
        offset += length
        count += 1
      end
      require_contract(count.positive?, 'AP contains no NAL units')
    elsif type == 49
      summary[:fragmentation_packets] += 1
      require_contract(payload.bytesize >= 4, 'FU header or data is truncated')
      fu_header = payload.getbyte(2)
      start = (fu_header & 128) != 0
      finish = (fu_header & 64) != 0
      original_type = fu_header & 63
      require_contract(!(start && finish) && original_type <= 47, 'invalid FU header')
      require_contract(!rtp[:marker] || finish, 'RTP marker precedes final FU fragment')
      unless start || finish
        summary[:middle_fu_packets] += 1
        summary[:middle_fu_types][original_type] += 1
        summary[:middle_vcl_fu_packets] += 1 if original_type < 32
      end
      reconstructed_header = [(payload.getbyte(0) & 129) | (original_type << 1), payload.getbyte(1)].pack('CC')
      if !start && fu && extended == fu[:sequence] + 1
        require_contract(rtp[:timestamp] == fu[:timestamp] && reconstructed_header == fu[:header],
                         'contiguous FU changed timestamp or NAL header')
      end
      if start
        summary[:incomplete_nals] += 1 if fu
        fu = {bytes: reconstructed_header + payload.byteslice(3..), sequence: extended,
              timestamp: rtp[:timestamp], header: reconstructed_header}
      elsif fu && extended == fu[:sequence] + 1 && rtp[:timestamp] == fu[:timestamp] &&
            reconstructed_header == fu[:header]
        require_contract(payload.bytesize - 3 <= MAX_NAL_BYTES - fu[:bytes].bytesize,
                         'reconstructed FU exceeds NAL byte limit')
        fu[:bytes] << payload.byteslice(3..)
        fu[:sequence] = extended
        if finish
          emit_nal.call(fu[:bytes])
          fu = nil
        end
      else
        summary[:incomplete_nals] += 1 if fu
        fu = nil
        summary[:orphan_fragments] += 1
      end
    else
      raise ContractError, "unsupported HEVC packetization type #{type}; DONL and PACI are not supported"
    end
  end
  summary[:incomplete_nals] += 1 if fu
  summary[:ssrc] = stream_ssrc
  summary[:annex_b_sha256] = nal_digest.hexdigest
  require_contract(summary[:packet_count] >= options[:min_packets], 'too few RTP packets')
  require_contract(summary[:complete_nals] >= options[:min_nals], 'too few complete NAL units')
  unless options[:loss]
    %i[duplicate_packets reordered_packets sequence_gaps incomplete_nals orphan_fragments
       timestamp_changes_without_marker].each do |key|
      require_contract(summary[key].zero?, "lossless RTP contract failed: #{key}=#{summary[key]}")
    end
    require_contract(previous_marker, 'capture ended before an RTP marker')
  end
  if options[:compare]
    missing = received.select { |_, value| value[2].zero? }.keys
    expected = received.select { |_, value| (value[1] & 4) != 0 }.keys
    held = received.select { |_, value| (value[1] & 16) != 0 && value[2].zero? }.keys
    require_contract(missing.sort == (expected + held).uniq.sort,
                     'a received packet disappeared without a recorded drop or final hold')
    summary[:forwarding] = {received: received.length, absent_ordinals: missing,
                            duplicate_ordinals: received.select { |_, value| value[2] == 2 }.keys,
                            reordered_records: comparison_reorders, exact_payloads: true}
  end
  nal_io.close if nal_io
  nal_io = nil
  puts JSON.pretty_generate(summary)
rescue ContractError, OptionParser::ParseError, SystemCallError, IOError => e
  warn "check-rtp: #{e.message}"
  exit 1
ensure
  nal_io.close if nal_io && !nal_io.closed?
end
