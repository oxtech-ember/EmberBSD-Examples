#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD contributors (AI-assisted).
require 'digest'
require 'json'
require 'open3'
require 'rbconfig'
require 'rexml/document'
require 'shellwords'

abort 'Usage: analyze.rb SOURCE BUILD NEW_OUT' unless ARGV.length == 3

def save_json(path, value)
  File.write(path, "#{JSON.pretty_generate(value)}\n")
end

def absolute_file(path, directory)
  File.realpath(File.expand_path(path, directory))
end

def clean_arguments(arguments)
  result = []
  skip = false
  arguments.drop(1).each do |argument|
    if skip
      skip = false
      next
    end
    if %w[-o -MF -MT -MQ -MJ --serialize-diagnostics].include?(argument)
      skip = true
      next
    end
    next if %w[-c -MD -MMD -MP -M -MM -MG].include?(argument)
    next if argument.match?(/\A(?:-o.+|-M[FQTJ].+|-Wp,-M(?:D|MD),.+)\z/)
    result << argument
  end
  raise 'Compile command ends with an output option' if skip
  result
end

def run_command(output, name, arguments, directory)
  save_json(File.join(output, "#{name}.command.json"),
            { 'directory' => directory, 'arguments' => arguments })
  result = { 'name' => name, 'arguments' => arguments, 'directory' => directory }
  File.open(File.join(output, "#{name}.stdout"), 'wb') do |stdout|
    File.open(File.join(output, "#{name}.stderr"), 'wb') do |stderr|
      begin
        pid = Process.spawn(*arguments, chdir: directory, out: stdout, err: stderr)
        status = Process.wait2(pid)[1]
        result.merge!('success' => status.success?, 'exit_code' => status.exitstatus,
                      'term_signal' => status.termsig)
      rescue SystemCallError => error
        result.merge!('success' => false, 'launch_error' => "#{error.class}: #{error.message}")
        stderr.puts(result['launch_error'])
      end
    end
  end
  text = File.read(File.join(output, "#{name}.stderr"))
  result['diagnostic_lines'] = text.lines.grep(/(?:fatal error|error|warning):/).map(&:chomp)
  save_json(File.join(output, "#{name}.status.json"), result)
  result
end

def successful_text(result, output)
  raise "Command failed: #{result['name']}" unless result['success']
  File.read(File.join(output, "#{result['name']}.stdout"))
end

def implicit_includes(path)
  paths = []
  inside = false
  File.foreach(path) do |line|
    inside = true if line.include?('#include <...> search starts here:')
    break if inside && line.include?('End of search list.')
    next unless inside && line.start_with?(' ')
    directory = line.strip
    raise "Unsupported framework include path: #{directory}" if directory.end_with?(' (framework directory)')
    raise "Compiler include directory is missing: #{directory}" unless File.directory?(directory)
    paths << File.realpath(directory)
  end
  raise "No actual compiler system include search paths in #{path}" if paths.empty?
  paths.uniq
end

def cppcheck_findings(path)
  document = REXML::Document.new(File.read(path))
  findings = []
  metadata = []
  document.elements.each('results/errors/error') do |error|
    item = { 'id' => error.attributes['id'], 'severity' => error.attributes['severity'],
             'message' => error.attributes['msg'],
             'locations' => error.get_elements('location').map do |location|
               location.attributes.map { |key, value| [key, value.to_s] }.to_h
             end }
    # This entry reports checker coverage, rather than a source diagnostic.
    (item['id'] == 'checkersReport' ? metadata : findings) << item
  end
  [findings, metadata]
end

def clang_findings(path)
  document = REXML::Document.new(File.read(path))
  dictionary = document.elements['plist/dict']
  raise 'Clang did not produce a plist dictionary' unless dictionary
  dictionary.elements.to_a.each_slice(2) do |key, value|
    next unless key.name == 'key' && key.text == 'diagnostics'
    raise 'Clang plist diagnostics are not an array' unless value&.name == 'array'
    return value.get_elements('dict').length
  end
  raise 'Clang plist has no diagnostics array'
end

output = nil
summary = { 'schema' => 'emberbsd.qt-four-streams.analysis.v1', 'checks' => [] }
begin
  source = File.realpath(ARGV[0])
  build = File.realpath(ARGV[1])
  database_path = File.join(build, 'compile_commands.json')
  database = JSON.parse(File.read(database_path))
  raise 'Compilation database must be an array' unless database.is_a?(Array)
  selected = %w[main.cpp i420-fixture.c].map do |name|
    file = File.realpath(File.join(source, name))
    matches = database.select do |entry|
      absolute_file(entry.fetch('file'), entry.fetch('directory')) == file
    end
    raise "Expected one actual compile command for #{name}, found #{matches.length}" unless matches.length == 1
    entry = matches.first
    arguments = entry['arguments'] || Shellwords.split(entry.fetch('command'))
    raise "Invalid argument array for #{name}" unless arguments.is_a?(Array) && arguments.all? { |item| item.is_a?(String) } && arguments.length > 1
    raise "Missing -c in actual compile command for #{name}" unless arguments.include?('-c')
    { 'name' => name, 'file' => file, 'directory' => File.realpath(entry.fetch('directory')),
      'arguments' => arguments, 'original' => entry }
  end
  requested_output = File.expand_path(ARGV[2])
  Dir.mkdir(requested_output, 0o755) # Refuse existing paths, including symlinks.
  output = requested_output
  summary['source'] = source
  summary['build'] = build
  summary['compile_database_sha256'] = Digest::SHA256.file(database_path).hexdigest
  summary['source_sha256'] = selected.to_h { |entry| [entry['name'], Digest::SHA256.file(entry['file']).hexdigest] }
  summary['script_sha256'] = Digest::SHA256.file(__FILE__).hexdigest
  summary['host_cpu'] = RbConfig::CONFIG.fetch('host_cpu')
  summary['host_os'] = RbConfig::CONFIG.fetch('host_os')
  save_json(File.join(output, 'compile_commands.original.json'), selected.map { |entry| entry['original'] })
  cppcheck = ENV.fetch('CPPCHECK', 'cppcheck')
  clang = ENV.fetch('CLANG', 'clang-19')
  cppcheck_version = run_command(output, 'cppcheck-version', [cppcheck, '--version'], build)
  summary['checks'] << cppcheck_version
  successful_text(cppcheck_version, output)
  clang_version = run_command(output, 'clang-version', [clang, '--version'], build)
  summary['checks'] << clang_version
  raise 'Clang19 is required' unless successful_text(clang_version, output).match?(/\bclang version 19\./)
  selected.each do |entry|
    tag = entry['name'].tr('.', '-')
    compiler = entry['arguments'].first
    version = run_command(output, "#{tag}-compiler-version", [compiler, '--version'], entry['directory'])
    summary['checks'] << version
    successful_text(version, output)
    full_version = run_command(output, "#{tag}-compiler-fullversion", [compiler, '-dumpfullversion', '-dumpversion'], entry['directory'])
    summary['checks'] << full_version
    raise "GCC16 is required for #{entry['name']}" unless successful_text(full_version, output).strip.match?(/\A16\./)
    target = run_command(output, "#{tag}-compiler-target", [compiler, '-dumpmachine'], entry['directory'])
    summary['checks'] << target
    target_text = successful_text(target, output).strip
    host_os = summary['host_os']
    expected_os = host_os.include?('linux') ? 'linux' : host_os.include?('netbsd') ? 'netbsd' : nil
    raise "Unsupported native host OS: #{host_os}" unless expected_os
    raise "Cross compiler is not a native analysis context: #{target_text}" unless target_text.include?(expected_os)
    aliases = { 'amd64' => 'x86_64', 'arm64' => 'aarch64' }
    compiler_cpu = target_text.split('-').first
    host_cpu = summary['host_cpu']
    raise "Compiler CPU differs from native host: #{target_text}" unless aliases.fetch(compiler_cpu, compiler_cpu) == aliases.fetch(host_cpu, host_cpu)
    arguments = clean_arguments(entry['arguments'])
    entry['clean_arguments'] = arguments
    context = File.join(output, "#{tag}.#{entry['name'].end_with?('.cpp') ? 'ii' : 'i'}")
    preprocess = run_command(output, "#{tag}-preprocess",
                             [compiler, *arguments, '-E', '-v', '-o', context], entry['directory'])
    summary['checks'] << preprocess
    raise "Actual preprocessing failed for #{entry['name']}" unless preprocess['success']
    entry['system_includes'] = implicit_includes(File.join(output, "#{tag}-preprocess.stderr"))
    macros = run_command(output, "#{tag}-macros",
                         [compiler, *arguments, '-E', '-dM'], entry['directory'])
    summary['checks'] << macros
    raise "Actual macro capture failed for #{entry['name']}" unless macros['success']
    entry['context_sha256'] = Digest::SHA256.file(context).hexdigest
  end
  summary['contexts'] = selected.map { |entry| entry.reject { |key, _| key == 'original' } }
  selected.each do |entry|
    tag = entry['name'].tr('.', '-')
    # Cppcheck2.17 does not import -isystem search paths from this database.
    # Keep the original compiler database untouched. Derive ordinary -I paths
    # for Cppcheck only, with the same observed directories and search order.
    cppcheck_arguments = entry['arguments'].map do |argument|
      argument == '-isystem' ? '-I' : argument.sub(/\A-isystem(?=.)/, '-I')
    end
    cppcheck_arguments += entry['system_includes'].flat_map { |path| ['-I', path] }
    imported = { 'directory' => entry['directory'], 'file' => entry['file'],
                 'arguments' => cppcheck_arguments }
    imported_path = File.join(output, "#{tag}-cppcheck-commands.json")
    save_json(imported_path, [imported])
    checked = run_command(output, "#{tag}-cppcheck",
                          [cppcheck, "--project=#{imported_path}", '--enable=all', '--inconclusive',
                           '--check-level=exhaustive', '--error-exitcode=1', '--xml', '--xml-version=2'], build)
    summary['checks'] << checked
    checked['derived_database_reason'] = 'Cppcheck2.17 does not import -isystem directories; use identical observed paths as -I for this tool only.'
    checked['original_database'] = 'compile_commands.original.json'
    findings, metadata = cppcheck_findings(File.join(output, "#{tag}-cppcheck.stderr"))
    checked['findings'] = findings
    checked['checker_metadata'] = metadata
  end
  c_entry = selected.find { |entry| entry['name'] == 'i420-fixture.c' }
  gcc = run_command(output, 'gcc16-analyzer',
                    [c_entry['arguments'].first, *c_entry['clean_arguments'], '-fanalyzer', '-c',
                     '-o', File.join(output, 'gcc16-analyzed.o')], c_entry['directory'])
  summary['checks'] << gcc
  cpp_entry = selected.find { |entry| entry['name'] == 'main.cpp' }
  # Replace implicit headers with the directories observed from the real GCC16
  # command. Do not add a target triple or substitute older C++ headers.
  clang_arguments = [clang, *cpp_entry['clean_arguments'], '-nostdinc',
                     *cpp_entry['system_includes'].flat_map { |path| ['-isystem', path] },
                     '--analyze', '-Xanalyzer', '-analyzer-output=plist',
                     '-o', File.join(output, 'clang19.plist')]
  analyzed = run_command(output, 'clang19-analyzer', clang_arguments, cpp_entry['directory'])
  if File.file?(File.join(output, 'clang19.plist'))
    analyzed['analyzer_findings'] = clang_findings(File.join(output, 'clang19.plist'))
  else
    analyzed['success'] = false
    analyzed['missing_plist'] = true
  end
  summary['checks'] << analyzed
  summary['pass'] = summary['checks'].all? do |check|
    check['success'] && check['diagnostic_lines'].empty? && check.fetch('findings', []).empty? \
      && check.fetch('analyzer_findings', 0).zero?
  end
rescue StandardError => error
  summary['pass'] = false
  summary['error'] = "#{error.class}: #{error.message}"
  warn summary['error']
ensure
  save_json(File.join(output, 'results.json'), summary) if output
end
puts JSON.generate(summary)
exit(summary['pass'] ? 0 : 1)
