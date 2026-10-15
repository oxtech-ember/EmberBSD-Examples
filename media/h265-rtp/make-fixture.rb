#!/usr/bin/env ruby
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 EmberBSD contributors.

require 'digest'
require 'json'
require 'optparse'
require 'tempfile'

WIDTH = 96
HEIGHT = 64
FPS = 10
GOP_FRAMES = 4
MAX_FRAMES = 4096
FRAME_BYTES = WIDTH * HEIGHT * 3 / 2

frames = 40
parser = OptionParser.new do |p|
  p.banner = 'Usage: make-fixture.rb OUTPUT.i420 METADATA.json [--frames N]'
  p.on('--frames N') do |n|
    raise OptionParser::InvalidArgument, 'frame count must use decimal digits' unless n.match?(/\A[0-9]+\z/)
    frames = Integer(n, 10)
  end
end

raw_temp = metadata_temp = nil
begin
  parser.parse!
  raise ArgumentError, parser.banner unless ARGV.length == 2
  raise ArgumentError, 'frame count must be in 1..4096' unless frames.between?(1, MAX_FRAMES)
  output, metadata_path = ARGV.map { |p| File.expand_path(p) }
  raise ArgumentError, 'output and metadata paths must differ' if output == metadata_path
  raise ArgumentError, 'output path already exists' if File.exist?(output) || File.symlink?(output)
  raise ArgumentError, 'metadata path already exists' if File.exist?(metadata_path) || File.symlink?(metadata_path)

  raw_temp = Tempfile.new(['.e35-fixture-', '.i420'], File.dirname(output))
  raw_temp.binmode
  digest = Digest::SHA256.new
  frames.times do |id|
    pixels = String.new(capacity: FRAME_BYTES, encoding: Encoding::BINARY)
    HEIGHT.times do |y|
      WIDTH.times do |x|
        pixels << ((17 * x + 31 * y + 13 * id + ((x ^ y) * id) % 256) % 256)
      end
    end
    (HEIGHT / 2).times do |y|
      (WIDTH / 2).times { |x| pixels << ((73 + 7 * x + 19 * y + 5 * id) % 256) }
    end
    (HEIGHT / 2).times do |y|
      (WIDTH / 2).times { |x| pixels << ((151 + 11 * x + 3 * y + 17 * id) % 256) }
    end
    pixels[0, 8] = 'E35F'.b + [id].pack('n') + [(id ^ 0xa5) & 255, (id + 0x5a) & 255].pack('CC')
    raise 'internal frame size error' unless pixels.bytesize == FRAME_BYTES
    raw_temp.write(pixels)
    digest.update(pixels)
  end
  raw_temp.flush
  raw_temp.fsync
  metadata = {
    format: 'I420', width: WIDTH, height: HEIGHT, frames: frames,
    fps_num: FPS, fps_den: 1, gop_frames: GOP_FRAMES,
    frame_bytes: FRAME_BYTES, total_bytes: frames * FRAME_BYTES,
    sha256: digest.hexdigest,
    frame_id: 'First eight Y bytes: ASCII E35F; uint16 BE id; low8(id XOR 0xa5); low8(id+0x5a)',
    y: '(17*x+31*y+13*id+(((x XOR y)*id) modulo 256)) modulo 256',
    u: '(73+7*x+19*y+5*id) modulo 256',
    v: '(151+11*x+3*y+17*id) modulo 256',
    chroma_coordinates: 'x and y are local coordinates in each half-width, half-height plane',
    gop_note: 'Configure the encoder for GOP 4; raw I420 has no encoded GOP or timestamps'
  }
  metadata_temp = Tempfile.new(['.e35-fixture-', '.json'], File.dirname(metadata_path))
  metadata_temp.write(JSON.pretty_generate(metadata) + "\n")
  metadata_temp.flush
  metadata_temp.fsync
  # Hard-link publication fails if either caller-selected destination exists.
  File.link(raw_temp.path, output)
  begin
    File.link(metadata_temp.path, metadata_path)
  rescue StandardError
    File.unlink(output) if File.identical?(output, raw_temp.path)
    raise
  end
  puts JSON.generate(metadata)
rescue ArgumentError, OptionParser::ParseError, SystemCallError, IOError => e
  warn "make-fixture: #{e.message}"
  exit 1
ensure
  raw_temp.close! if raw_temp
  metadata_temp.close! if metadata_temp
end
