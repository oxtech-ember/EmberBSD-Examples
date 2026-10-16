#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD contributors (AI-assisted).
require 'json'
require 'csv'

errors = []
observed = []
check = ->(condition, message) { errors << message unless condition }
number = lambda do |value, name|
  raise "Invalid numeric field: #{name}" unless value.is_a?(Numeric) && value.finite?
  value
end
positive = lambda do |text|
  raise 'Expected a finite positive decimal number' unless text&.match?(/\A(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+)(?:[eE][+-]?[0-9]+)?\z/)
  value = Float(text)
  raise 'Expected a finite positive decimal number' unless value.finite? && value.positive?
  value
end
begin
  raise 'Usage: check-report.rb REPORT_JSON EXPECTED_FRAMES NOMINAL_FPS MINIMUM_OBSERVED_FPS' unless ARGV.length == 4
  path, count_text, nominal_text, minimum_text = ARGV
  raise 'EXPECTED_FRAMES must be a positive integer' unless count_text.match?(/\A[1-9][0-9]*\z/)
  expected = Integer(count_text, 10)
  nominal, minimum = [nominal_text, minimum_text].map { |text| positive.call(text) }
  report = JSON.parse(File.read(path))
  check.call(report.fetch('schema') == 'emberbsd.qt-four-streams.v1', 'Unexpected report schema')
  %w[pass started full_pixel_verification require_all_fixture_frames cpu_usage_valid].each do |key|
    check.call(report.fetch(key) == true, "#{key} must be true")
  end
  check.call(report.fetch('exit_code') == 0 && report.fetch('completion') == 'all-eof' && report.fetch('requested_completion') == 'all-eof', 'Run did not complete successfully at EOF')
  check.call(number.call(report.fetch('fixture_nominal_fps'), 'fixture_nominal_fps') == nominal, 'Fixture nominal FPS mismatch')
  wall = number.call(report.fetch('wall_us'), 'wall_us')
  exposure = number.call(report.fetch('first_exposure_us'), 'first_exposure_us')
  check.call(wall.positive? && exposure >= 0 && exposure <= wall, 'Invalid wall duration or exposure')
  %w[four_players_playing_overlap_us four_delivery_intervals_overlap_us].each do |key|
    value = number.call(report.fetch(key), key)
    check.call(value.positive? && value <= wall, "Invalid #{key}")
  end
  cpu = number.call(report.fetch('cpu_percent_one_core_100'), 'cpu_percent_one_core_100')
  cpu_times = %w[cpu_user_us cpu_system_us].map { |key| number.call(report.fetch(key), key) }
  check.call(cpu.positive? && cpu_times.all? { |value| value >= 0 }, 'Invalid measured CPU usage')
  check.call(wall.positive? && (cpu - cpu_times.sum * 100.0 / wall).abs <= 1e-6, 'CPU percentage does not match actual CPU/wall times')
  streams = report.fetch('streams')
  check.call(streams.is_a?(Array) && streams.length == 4 && streams.all? { |s| s.fetch('stream').is_a?(Integer) } && streams.map { |s| s.fetch('stream') }.sort == [0, 1, 2, 3], 'Expected exactly four distinct stream IDs 0..3')
  raise 'Invalid stream identities' unless errors.none? { |message| message.include?('stream IDs') }
  csv_path = File.join(File.dirname(path), 'frames.csv')
  raise 'frames.csv exceeds 64 MiB' if File.size(csv_path) > 64 * 1024 * 1024
  table = CSV.read(csv_path, headers: true)
  columns = %w[event stream delivery frame_id pts_us end_us wall_us width height pixel_format verified]
  raise 'Unexpected frames.csv header' unless table.headers == columns
  rows = table.map do |row|
    raise 'Unexpected CSV event' unless %w[frame paint].include?(row['event'])
    values = columns.drop(1).to_h do |key|
      text = row[key]
      raise "Invalid CSV integer: #{key}" unless text&.match?(/\A-?[0-9]+\z/)
      [key, Integer(text, 10)]
    end
    raise 'Invalid CSV stream ID' unless (0..3).cover?(values['stream'])
    values.merge('event' => row['event'])
  end
  intervals = []
  streams.each do |stream|
    id = stream.fetch('stream')
    prefix = "stream #{id}: "
    check.call(stream.fetch('eof') == true && stream.fetch('media_status') == 'EndOfMedia', prefix + 'Missing actual EOF')
    check.call(stream.fetch('player_error_text') == '', prefix + 'Player error text')
    %w[player_error equal_pts regressing_pts missing_pts mailbox_coalesced_frames mailbox_discarded_on_stop fixture_missing_frames delivered_not_painted_frames].each do |key|
      check.call(stream.fetch(key) == 0, prefix + key + ' must be zero')
    end
    %w[fixture_expected_frames fixture_verified_frames delivered_frames painted_distinct_frames sink_valid_frame_notifications mailbox_dequeued_frames].each do |key|
      check.call(stream.fetch(key) == expected, prefix + key + ' count mismatch')
    end
    check.call(stream.fetch('fixture_missing_frame_ids') == [] && stream.fetch('frame_handle_type') == 0, prefix + 'Missing IDs or non-CPU frame handle')
    check.call(number.call(stream.fetch('video_frame_rate_metadata'), prefix + 'metadata FPS') == nominal, prefix + 'Nominal metadata FPS mismatch')
    %w[width height].each { |key| check.call(stream.fetch(key).is_a?(Integer) && stream.fetch(key).positive?, prefix + 'Invalid ' + key) }
    deliveries = rows.select { |row| row['stream'] == id && row['event'] == 'frame' }
    paints = rows.select { |row| row['stream'] == id && row['event'] == 'paint' }
    intervals << [deliveries.first['wall_us'], deliveries.last['wall_us']] unless deliveries.empty?
    { 'delivery' => deliveries, 'paint' => paints }.each do |kind, events|
      check.call(events.length == expected, prefix + kind + ' CSV count mismatch')
      check.call(events.each_with_index.all? { |row, index| row['delivery'] == index + 1 && row['frame_id'] == index && row['verified'] == 1 && row['width'] == stream['width'] && row['height'] == stream['height'] && row['pixel_format'] == stream.fetch('pixel_format') && row['pts_us'] >= 0 && row['wall_us'] >= exposure && row['wall_us'] <= wall }, prefix + kind + ' CSV identity, verification, dimensions or bounds mismatch')
      check.call(events.each_cons(2).all? { |a, b| b['pts_us'] > a['pts_us'] && b['wall_us'] > a['wall_us'] }, prefix + kind + ' PTS or wall time did not increase')
      first, last = events.first, events.last
      next unless first && last
      check.call(stream.fetch("first_#{kind}_us") == first['wall_us'] && stream.fetch("last_#{kind}_us") == last['wall_us'], prefix + kind + ' report/CSV time mismatch')
      span = last['wall_us'] - first['wall_us']
      fps = span.positive? ? (events.length - 1) * 1_000_000.0 / span : 0.0
      stated = number.call(stream.fetch("observed_#{kind}_fps"), prefix + kind + ' FPS')
      check.call((stated - fps).abs <= [1e-6, fps.abs * 1e-9].max, prefix + kind + ' report/CSV FPS mismatch')
      check.call(fps >= minimum && stated >= minimum, prefix + kind + ' FPS below explicit minimum')
      observed << { stream: id, event: kind, count: events.length, csv_fps: fps, report_fps: stated }
    end
    check.call(paints.each_with_index.all? { |paint, index| frame = deliveries[index]; frame && %w[delivery frame_id pts_us width height pixel_format verified].all? { |key| paint[key] == frame[key] } && paint['wall_us'] >= frame['wall_us'] }, prefix + 'Paint does not refer to an actual delivered frame')
  end
  overlap = intervals.length == 4 ? intervals.map(&:last).min - intervals.map(&:first).max : 0
  check.call(overlap.positive? && overlap == report.fetch('four_delivery_intervals_overlap_us'), 'CSV delivery intervals do not match positive reported overlap')
rescue StandardError => error
  errors << "#{error.class}: #{error.message}"
end
puts JSON.pretty_generate({ pass: errors.empty?, report: ARGV[0], expected_frames: expected,
                           nominal_fps: nominal, minimum_observed_fps: minimum,
                           observed: observed, errors: errors })
exit(errors.empty? ? 0 : 1)
