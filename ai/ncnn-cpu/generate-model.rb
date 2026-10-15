#!/usr/bin/env ruby
# SPDX-License-Identifier: MIT
# The graph and all weights are original synthetic test data under LICENSE.
require 'fileutils'
abort "Usage: #{$PROGRAM_NAME} OUTPUT_DIRECTORY" unless ARGV.length == 1
out = ARGV.fetch(0)
FileUtils.mkdir_p(out)
graph = <<~GRAPH
  7767517
  6 6
  Input input 0 1 input 0=5 1=5 2=2
  Convolution conv 1 1 input conv 0=3 1=3 5=1 6=54
  ReLU relu 1 1 conv relu
  Pooling pool 1 1 relu pool 0=1 4=1
  InnerProduct dense 1 1 pool logits 0=3 1=1 2=9
  Softmax softmax 1 1 logits prob 0=0 1=1
GRAPH
conv = Array.new(54) { |i| ((i * 7 + 3) % 17 - 8) / 32.0 }
dense = Array.new(9) { |i| ((i * 5 + 1) % 11 - 5) / 16.0 }
weights = [0].pack('V') + conv.pack('e*') + [-0.125, 0.0625, 0.1875].pack('e*')
weights += [0].pack('V') + dense.pack('e*') + [0.0625, -0.125, 0.25].pack('e*')
abort 'Internal model size error' unless weights.bytesize == 284
File.binwrite(File.join(out, 'tiny-cnn.param'), graph)
File.binwrite(File.join(out, 'tiny-cnn.bin'), weights)
File.binwrite(File.join(out, 'malformed.param'), "0\n6 6\n")
File.binwrite(File.join(out, 'truncated.bin'), weights.byteslice(0, 280))
File.binwrite(File.join(out, 'nonfinite.bin'), weights.dup.tap { |s| s[4, 4] = [0x7fc00000].pack('V') })
puts 'Generated original 6-layer, 284-byte FP32 CNN weights.'
