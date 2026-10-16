#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD contributors (AI-assisted).
require 'json'
require 'digest'

# This sampled oracle consumes an actual P6 framebuffer capture. It excludes
# labels and letterboxes. Nearest mode preserves the original Qt oracle.
# Bilinear mode uses independent triangle-filter marker predictions and samples
# constant block interiors. FFmpeg's integer limited-range conversion can leave
# neutral R=B, G=R-2: check median brightness and neutral spread separately, each
# within two code values. Do not broaden the per-channel nearest-mode tolerance.
# The marker must still distinguish periodic body patterns and wrong channels.
sampling = ARGV[2] || 'nearest'
unless (2..3).cover?(ARGV.length) && %w[nearest bilinear].include?(sampling) &&
       (match = /\A([1-9][0-9]*)x([1-9][0-9]*):([1-9][0-9]*)\z/.match(ARGV[1]))
  warn 'Usage: check-window.rb CAPTURE.ppm FIXTURE_WIDTHxHEIGHT:FRAMES [nearest|bilinear]'
  exit 2
end

begin
  fixture_width, fixture_height, frames = match.captures.map(&:to_i)
  raise 'Invalid fixture dimensions or frame count' unless
    [fixture_width, fixture_height].all? { |n| (16..32_768).cover?(n) && n.even? } && (2..256).cover?(frames)
  raise 'PPM exceeds 64 MiB' if File.size(ARGV[0]) > 64 * 1024 * 1024
  bytes = File.binread(ARGV[0])
  position = 0
  token = lambda do
    loop do
      position += 1 while position < bytes.bytesize && " \t\r\n".include?(bytes.byteslice(position, 1))
      if bytes.getbyte(position) == 35
        newline = bytes.index("\n", position)
        raise 'Unterminated PPM comment' unless newline
        position = newline + 1
      else
        break
      end
      raise 'PPM header too large' if position > 65_536
    end
    first = position
    position += 1 while position < bytes.bytesize && !" \t\r\n#".include?(bytes.byteslice(position, 1))
    raise 'Invalid PPM header' if position == first || position > 65_536
    bytes.byteslice(first...position)
  end
  raise 'Expected binary P6 PPM' unless token.call == 'P6'
  width_text, height_text, max_text = 3.times.map { token.call }
  raise 'Invalid PPM numbers' unless [width_text, height_text, max_text].all? { |s| s.match?(/\A[0-9]+\z/) }
  width, height, max_value = [width_text, height_text, max_text].map(&:to_i)
  raise 'Expected an even RGB888 capture from 160x120 through 32768x32768' unless
    (160..32_768).cover?(width) && (120..32_768).cover?(height) && width.even? && height.even? && max_value == 255
  separator = bytes.byteslice(position, 2) == "\r\n" ? 2 : 1
  raise 'PPM header separator missing' unless " \t\r\n".include?(bytes.byteslice(position, 1))
  rgb = bytes.byteslice(position + separator..)
  raise 'PPM pixel byte count differs' unless rgb && rgb.bytesize == width * height * 3

  content_width = width / 2 - 8
  content_height = height / 2 - 32
  fitted_width = content_height * fixture_width / fixture_height
  if fitted_width <= content_width
    fitted_height = content_height
  else
    fitted_width = content_width
    fitted_height = content_width * fixture_height / fixture_width
  end
  raise 'Fixture has no visible destination' unless fitted_width.positive? && fitted_height.positive?
  scale = fitted_width.to_f / fixture_width
  triangle = lambda do |coordinate, source_size, destination_size|
    ratio = source_size.to_f / destination_size
    center = (coordinate + 0.5) * ratio - 0.5
    radius = [ratio, 1.0].max
    weights = ((center - radius).ceil..(center + radius).floor).filter_map do |pixel|
      weight = [0.0, 1.0 - (pixel - center).abs / radius].max
      [[[pixel, 0].max, source_size - 1].min, weight] if weight.positive?
    end
    total = weights.sum(&:last)
    weights.map { |pixel, weight| [pixel, weight / total] }
  end
  # Use actual block interiors. Small fixtures get several interior points.
  points = []
  (0...fixture_height).step(64) do |block_y|
    (0...fixture_width).step(64) do |block_x|
      block_width = [64, fixture_width - block_x].min
      block_height = [64, fixture_height - block_y].min
      fractions = fixture_width <= 128 && fixture_height <= 128 ? [0.25, 0.5, 0.75] : [0.5]
      fractions.each do |fy|
        fractions.each do |fx|
          points << [block_x + (block_width * fx).floor, block_y + (block_height * fy).floor]
        end
      end
    end
  end
  if points.length > 192
    original = points
    points = 192.times.map { |i| original[i * original.length / 192] }
  end
  # Sample the actual visible marker pixels. Several source bits can map to one
  # displayed pixel, so the result can retain more than one possible frame ID.
  marker_resolved = scale * 2 >= 2 && fitted_height.to_f / fixture_height * 16 >= 4
  points.concat(8.times.map { |bit| [2 * bit + 1, 8] }) if marker_resolved
  points.uniq!

  channels = 4.times.map do |channel|
    column, row = channel % 2, channel / 2
    center_x = column * (width / 2) + (4 + width / 2 - 5) / 2
    center_y = row * (height / 2) + (28 + height / 2 - 5) / 2
    destination_x = center_x - fitted_width / 2
    destination_y = center_y - fitted_height / 2
    samples = points.filter_map do |source_x, source_y|
      local_x = [((source_x + 0.5) * fitted_width / fixture_width).floor, fitted_width - 1].min
      local_y = [((source_y + 0.5) * fitted_height / fixture_height).floor, fitted_height - 1].min
      actual_x = ((local_x + 0.5) * fixture_width / fitted_width).floor
      actual_y = ((local_y + 0.5) * fixture_height / fitted_height).floor
      next if actual_x < 16 && actual_y < 16
      if sampling == 'bilinear'
        xs = triangle.call(local_x, fixture_width, fitted_width)
        ys = triangle.call(local_y, fixture_height, fitted_height)
        # Every filter tap must remain in one constant fixture block and must
        # exclude the marker. This avoids accepting edge blends as raw pixels.
        next unless xs.all? { |x, _| x / 64 == actual_x / 64 } &&
                    ys.all? { |y, _| y / 64 == actual_y / 64 }
        next if xs.any? { |x, _| x < 16 } && ys.any? { |y, _| y < 16 }
      end
      screen_x, screen_y = destination_x + local_x, destination_y + local_y
      pixel = rgb.byteslice((screen_y * width + screen_x) * 3, 3).bytes
      [actual_x, actual_y, screen_x, screen_y, pixel, nil]
    end
    marker_columns = [(16.0 * fitted_width / fixture_width).ceil, fitted_width].min
    marker_rows = [(16.0 * fitted_height / fixture_height).ceil, fitted_height].min
    if marker_columns.positive? && marker_rows.positive?
      marker_y = marker_rows / 2
      marker_columns.times do |marker_x|
        actual_x = ((marker_x + 0.5) * fixture_width / fitted_width).floor
        actual_y = ((marker_y + 0.5) * fixture_height / fitted_height).floor
        next unless actual_x < 16 && actual_y < 16
        pixel = rgb.byteslice(((destination_y + marker_y) * width + destination_x + marker_x) * 3, 3).bytes
        taps = if sampling == 'bilinear'
                 triangle.call(marker_x, fixture_width, fitted_width).flat_map do |x, wx|
                   triangle.call(marker_y, fixture_height, fitted_height).map { |y, wy| [x, y, wx * wy] }
                 end
               end
        samples << [actual_x, actual_y, destination_x + marker_x, destination_y + marker_y, pixel, taps]
      end
    end
    samples.uniq!
    raise 'Too few independent samples; enlarge fixture' if samples.length < 9
    score = lambda do |frame, candidate_channel|
      mismatches = 0
      max_error = 0
      max_check_error = 0
      total_error = 0
      samples.each do |x, y, _screen_x, _screen_y, actual, taps|
        fixture_luma = lambda do |sx, sy|
          if sx < 16 && sy < 16
            (frame & (1 << (sx / 2))).zero? ? 16 : 235
          else
            16 + ((sx / 64 + 3 * (sy / 64) + 5 * frame + 41 * candidate_channel) % 220)
          end
        end
        luma = taps ? taps.sum { |sx, sy, weight| fixture_luma.call(sx, sy) * weight } : fixture_luma.call(x, y)
        expected = ((luma - 16) * 255.0 / 219).round
        rgb_errors = actual.map { |value| (value - expected).abs }
        errors = if sampling == 'bilinear'
                   [(actual.sort[1] - expected).abs, actual.max - actual.min]
                 else
                   rgb_errors
                 end
        mismatches += 1 if errors.max > 2
        max_error = [max_error, rgb_errors.max].max
        max_check_error = [max_check_error, errors.max].max
        total_error += rgb_errors.sum
      end
      { frame_id: frame, mismatched_points: mismatches, max_rgb_error: max_error,
        max_color_check_error: max_check_error, total_rgb_error: total_error }
    end
    candidates = frames.times.map { |frame| score.call(frame, channel) }
    matching = candidates.select { |candidate| candidate[:mismatched_points].zero? }
    best = candidates.min_by { |candidate| [candidate[:mismatched_points], candidate[:total_rgb_error]] }
    alternatives = (0..3).reject { |other| other == channel }.flat_map do |other|
      frames.times.filter_map do |frame|
        candidate = score.call(frame, other)
        { channel: other, frame_id: frame } if candidate[:mismatched_points].zero?
      end
    end
    { channel: channel, plausible: !matching.empty? && alternatives.empty?,
      frame_id: matching.length == 1 ? matching.first[:frame_id] : nil,
      matching_frame_ids: matching.map { |candidate| candidate[:frame_id] },
      ambiguous_other_channels: alternatives,
      sampled_points: samples.length, marker_resolved: marker_resolved,
      bilinear_marker_samples: samples.count { |sample| sample[5] },
      destination: [destination_x, destination_y, fitted_width, fitted_height], best_candidate: best }
  end
  result = { schema: 'emberbsd.qt-four-streams.window-check.v1',
             success: channels.all? { |channel| channel[:plausible] },
             capture_sha256: Digest::SHA256.hexdigest(bytes), width: width, height: height,
             fixture_width: fixture_width, fixture_height: fixture_height, frames: frames,
             sampling_model: sampling == 'bilinear' ? 'triangle-marker-and-constant-block-interiors' : 'nearest-displayed-pixels',
             color_check: sampling == 'bilinear' ? 'median-brightness-and-neutral-rgb-spread' : 'each-rgb-channel',
             rgb_rounding_tolerance: sampling == 'nearest' ? 2 : nil,
             brightness_rounding_tolerance: sampling == 'bilinear' ? 2 : nil,
             neutral_rgb_spread_tolerance: sampling == 'bilinear' ? 2 : nil,
             channels: channels,
             limitation: 'Sampled scaled-image plausibility; scaling can leave ambiguous IDs or channels. Ambiguous channels fail. No display timing or FPS claim.' }
  puts JSON.pretty_generate(result)
  exit(result[:success] ? 0 : 1)
rescue StandardError => error
  puts JSON.pretty_generate({ success: false, error: "#{error.class}: #{error.message}" })
  exit 1
end
