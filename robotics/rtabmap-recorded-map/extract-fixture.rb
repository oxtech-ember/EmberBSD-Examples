#!/usr/bin/env ruby
# SPDX-License-Identifier: MIT
# Origin: EmberBSD (AI-assisted). Select unchanged recorded TUM RGB-D frames.
require 'digest'
require 'fileutils'
require 'json'
require 'open3'

abort 'Usage: extract-fixture.rb TUM_ARCHIVE NEW_FIXTURE' unless ARGV.length == 2
archive, output = ARGV
[archive, output].each do |path|
  abort 'Simple absolute paths are required.' unless path.match?(%r{\A/[A-Za-z0-9_./-]+\z}) && File.expand_path(path) == path
end
abort 'Fixture path already exists.' if File.exist?(output) || File.symlink?(output)
abort 'A regular source archive is required.' unless File.file?(archive) && !File.symlink?(archive)
expected = File.read(File.join(__dir__, 'dataset.tsv')).split[1]
abort 'Dataset archive SHA256 mismatch.' unless Digest::SHA256.file(archive).hexdigest == expected
root = 'rgbd_dataset_freiburg1_desk'
read_entry = lambda do |path|
  text, diagnostics, status = Open3.capture3('tar', '-xOzf', archive, "#{root}/#{path}")
  raise "Cannot read #{path}: #{diagnostics}" unless status.success?
  text
end
lists = %w[rgb.txt depth.txt groundtruth.txt].to_h { |name| [name, read_entry.call(name)] }
parse = lambda do |text, count|
  last = -Float::INFINITY
  text.lines.filter_map do |line|
    next if line.start_with?('#') || line.strip.empty?
    fields = line.split
    raise 'Malformed timestamp list' unless fields.length == count
    stamp = Float(fields[0])
    raise 'Nonfinite or unordered timestamp' unless stamp.finite? && stamp > last
    last = stamp
    fields
  end
end
rgb = parse.call(lists.fetch('rgb.txt'), 2)
depth = parse.call(lists.fetch('depth.txt'), 2)
ground = parse.call(lists.fetch('groundtruth.txt'), 8)
used_rgb = {}
pairs = depth.filter_map do |d|
  candidate = rgb.reject { |v| used_rgb[v[1]] }.min_by { |v| [(Float(v[0]) - Float(d[0])).abs, Float(v[0])] }
  next unless candidate && (Float(candidate[0]) - Float(d[0])).abs <= 0.020
  pose = ground.min_by { |v| [(Float(v[0]) - Float(candidate[0])).abs, Float(v[0])] }
  next unless pose && (Float(pose[0]) - Float(candidate[0])).abs <= 0.020
  used_rgb[candidate[1]] = true
  [candidate, d, pose]
end
selected = (0...12).map { |i| pairs.fetch(i * 10) }
selected.each do |color, distance, pose|
  raise 'Unexpected image path' unless color[1].match?(%r{\Argb/[0-9]+\.[0-9]+\.png\z}) && distance[1].match?(%r{\Adepth/[0-9]+\.[0-9]+\.png\z})
  raise 'Nonfinite recorded pose' unless pose.drop(1).all? { |v| Float(v).finite? }
end
FileUtils.mkdir_p(output)
%w[rgb depth].each { |name| Dir.mkdir(File.join(output, name)) }
entries = selected.flat_map { |color, distance, _pose| [color[1], distance[1]] }
_stdout, diagnostics, status = Open3.capture3('tar', '-xzf', archive, '-C', output, '--strip-components=1', *entries.map { |entry| "#{root}/#{entry}" })
raise "Cannot extract selected images: #{diagnostics}" unless status.success?
lists.each { |name, text| File.write(File.join(output, name), text) }
header = "id rgb_stamp depth_stamp gt_stamp rgb depth tx ty tz qx qy qz qw\n"
rows = selected.each_with_index.map do |(color, distance, pose), index|
  [index + 1, color[0], distance[0], pose[0], color[1], distance[1], *pose.drop(1)].join(' ') + "\n"
end
File.write(File.join(output, 'frames.tsv'), header + rows.join)
files = (entries + %w[rgb.txt depth.txt groundtruth.txt frames.tsv]).sort.to_h do |name|
  [name, Digest::SHA256.file(File.join(output, name)).hexdigest]
end
File.write(File.join(output, 'inputs.sha256'), files.map { |name, hash| "#{hash}  #{name}\n" }.join)
receipt = {
  source_url: File.read(File.join(__dir__, 'dataset.tsv')).split[2],
  source_archive_sha256: expected,
  source_archive_bytes: File.size(archive),
  license: 'CC BY 4.0',
  attribution: 'J. Sturm, N. Engelhard, F. Endres, W. Burgard and D. Cremers. A Benchmark for the Evaluation of RGB-D SLAM Systems. IROS 2012.',
  selection: 'First 12 eligible pairs, stride 10. Nearest unused RGB within 20 ms of depth; nearest ground truth within 20 ms of RGB. Ties select the earlier timestamp.',
  frame_count: selected.length, first_rgb_stamp: selected.first[0][0], last_rgb_stamp: selected.last[0][0],
  unchanged_original_files: files
}
File.write(File.join(output, 'fixture.json'), JSON.pretty_generate(receipt) + "\n")
puts "PASS: #{selected.length} recorded RGB-D frames extracted without image modification"
