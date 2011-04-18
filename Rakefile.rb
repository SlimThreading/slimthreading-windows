require 'albacore'

CONFIGURATION = ENV['config'] || 'release'
PLATFORM = ENV['platform'] || 'x64'
SOLUTION = 'src/SlimThreading.sln'
STAGE_DIR = File.expand_path("build")

task :default => [:build]

desc "Prepares the working directory for a new build"
task :clean do
  clean_dir STAGE_DIR
end

desc "Compiles the library"
msbuild :compile => [:clean] do |msb|
  msb.properties :configuration => CONFIGURATION, :platform => PLATFORM
  msb.targets :Clean, :Build
  msb.solution = SOLUTION
end

desc 'Runs the tests'
task :test

desc "Compiles and runs the tests"
task :build => [:compile, :test] do
  Dir.glob(File.join(msbuild_output_path, "SlimThreading.{dll,lib,pdb,xml}")) do |f|
     copy f, STAGE_DIR if File.file? f
  end
  copy 'LICENSE.txt', STAGE_DIR
  copy 'README.md', STAGE_DIR
end

def msbuild_output_path
  return "src/X64/#{CONFIGURATION}/" if PLATFORM.downcase == 'x64'
	return "src/#{CONFIGURATION}/" if PLATFORM.downcase == 'win32'
	raise ArgumentError, 'Unkown platform ' + PLATFORM
end

def clean_dir(dir)
  Dir[dir + "/*"].each do |file|
    if Dir.exists?(file)
      clean_dir file
      Dir.delete file
    else
      File.delete file
    end
  end
  Dir.mkdir dir unless File.exist? dir
end