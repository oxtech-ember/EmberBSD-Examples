#!/usr/bin/env ruby
# SPDX-License-Identifier: MIT
# Encode this fixed graph with the public ONNX protobuf field layout.
# No exporter, downloaded model, learned weights, or protobuf tool is needed.
require 'digest'
require 'fileutils'

def varint(value)
  raise ArgumentError, 'negative protobuf varint' if value.negative?
  bytes = ''.b
  loop do
    byte = value & 127
    value >>= 7
    bytes << (byte | (value.zero? ? 0 : 128))
    break if value.zero?
  end
  bytes
end

def number(field, value)
  varint(field * 8) + varint(value)
end

def message(field, value)
  value = value.b
  varint(field * 8 + 2) + varint(value.bytesize) + value
end

def tensor(name, dimensions, values)
  raise ArgumentError, 'tensor element count' unless dimensions.inject(:*) == values.length
  dimensions.map { |dim| number(1, dim) }.join.b + number(2, 1) +
    message(8, name) + message(9, values.pack('e*'))
end

def value_info(name, columns)
  dimensions = message(1, message(2, 'batch')) + message(1, number(1, columns))
  message(1, name) + message(2, message(1, number(1, 1) + message(2, dimensions)))
end

def node(name, operator, inputs, output)
  inputs.map { |input| message(1, input) }.join.b +
    message(2, output) + message(3, name) + message(4, operator)
end

begin
  raise ArgumentError, 'usage: generate-model.rb OUTPUT_DIRECTORY' unless ARGV.length == 1 && !ARGV[0].empty?
  graph = message(1, node('dense1', 'Gemm', %w[x w1 b1], 'pre')) +
          message(1, node('activation', 'Relu', ['pre'], 'hidden')) +
          message(1, node('dense2', 'Gemm', %w[hidden w2 b2], 'y')) +
          message(2, 'ember-tiny-mlp') +
          message(5, tensor('w1', [3, 4], [1, -2, 0.5, 0, 0.5, 1, -1, 2, -1, 0, 2, -0.5])) +
          message(5, tensor('b1', [4], [0.25, -0.5, 1, 0])) +
          message(5, tensor('w2', [4, 2], [1, -1, 0.5, 2, -2, 0.25, 1.5, -0.5])) +
          message(5, tensor('b2', [2], [0.75, -1.25])) +
          message(11, value_info('x', 3)) + message(12, value_info('y', 2))
  model = number(1, 8) + message(2, 'EmberBSD ONNX CPU example') +
          message(6, 'Analytic 3-4-2 Gemm/Relu/Gemm; dynamic batch; ONNX opset 13') +
          message(7, graph) + message(8, number(2, 13))
  FileUtils.mkdir_p(ARGV[0])
  File.binwrite(File.join(ARGV[0], 'tiny-mlp.onnx'), model)
  # A truncated length-delimited protobuf field cannot be a valid ModelProto.
  File.binwrite(File.join(ARGV[0], 'malformed.onnx'), "\x3a\x7ftruncated".b)
  puts "#{Digest::SHA256.hexdigest(model)}  tiny-mlp.onnx (#{model.bytesize} bytes)"
rescue StandardError => error
  warn error.message
  exit 1
end
