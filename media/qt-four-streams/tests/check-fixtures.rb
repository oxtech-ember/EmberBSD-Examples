#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD contributors (AI-assisted).
require 'json'
require 'digest'
require 'open3'

abort 'Usage: check-fixtures.rb NATIVE_FFMPEG NATIVE_FFPROBE FIXTURES NEW_RESULTS' unless ARGV.length == 4
ffmpeg, probe, fixtures, output = ARGV
manifest = JSON.parse(File.read(fixtures + '/fixtures.json'))
abort 'Incomplete fixture generation' unless manifest.fetch('status') == 'complete'
Dir.mkdir(output)
results = []
manifest.fetch('channels').each do |channel|
  index = channel.fetch('channel'); abort 'Bad channel' unless (0..3).cover?(index)
  file = fixtures + '/' + channel.fetch('file')
  abort 'Encoded fixture digest differs' unless Digest::SHA256.file(file).hexdigest == channel.fetch('encoded_sha256')
  args = [probe, '-v', 'error', '-count_frames', '-show_streams', '-show_frames', '-select_streams', 'v:0', '-of', 'json', file]
  stdout, stderr, status = Open3.capture3(*args)
  File.write(output + "/channel#{index}-probe.json", stdout)
  File.write(output + "/channel#{index}-probe.stderr", stderr)
  abort 'Probe failed' unless status.success? && stderr.empty?
  data = JSON.parse(stdout); streams = data.fetch('streams')
  abort 'Expected one video stream' unless streams.size == 1
  stream = streams.first
  abort 'Wrong codec, size, rate or time base' unless stream.fetch('codec_name') == 'hevc' &&
    stream.fetch('width') == manifest.fetch('width') && stream.fetch('height') == manifest.fetch('height') &&
    Rational(stream.fetch('avg_frame_rate')) == manifest.fetch('fps') && stream.fetch('time_base') == '1/90000'
  frames = data.fetch('frames')
  abort 'Wrong actual frame count' unless frames.size == manifest.fetch('frames') && Integer(stream.fetch('nb_read_frames')) == frames.size
  frames.each_with_index do |frame, number|
    abort 'Wrong actual PTS' unless Integer(frame.fetch('pts')) == number * 90000 / manifest.fetch('fps')
  end
  digest = Digest::SHA256.new; bytes = 0
  command = [ffmpeg, '-nostdin', '-v', 'error', '-threads', '1', '-i', file, '-map', '0:v:0', '-an', '-pix_fmt', 'yuv420p', '-f', 'rawvideo', 'pipe:1']
  err = File.open(output + "/channel#{index}-decode.stderr", 'wb')
  begin
    Open3.popen2(*command, err: err) do |input, decoded, wait|
      input.close
      loop do
        block = decoded.read(65536); break unless block
        bytes += block.bytesize; digest.update(block)
      end
      abort 'Decoder failed' unless wait.value.success?
    end
  ensure
    err.close
  end
  abort 'Decoder emitted diagnostics' unless File.zero?(output + "/channel#{index}-decode.stderr")
  abort 'Decoded active pixels differ from original source stream' unless bytes == channel.fetch('raw_bytes') && digest.hexdigest == channel.fetch('raw_sha256')
  results << {channel: index, decoded_frames: frames.size, bytes: bytes, raw_sha256: digest.hexdigest, actual_pts_step: 90000 / manifest.fetch('fps')}
end
abort 'Four distinct channels required' unless results.map { |i| i[:channel] }.sort == [0, 1, 2, 3]
File.write(output + '/checked.json', JSON.pretty_generate(results) + "\n")
puts 'PASS: four actual HEVC streams, nominal dimensions/rate, exact PTS, frame counts and complete lossless decoded pixels'
