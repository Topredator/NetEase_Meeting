#
# To learn more about a Podspec see http://guides.cocoapods.org/syntax/podspec.html.
# Run 'pod lib lint meeting_plugin.podspec' to validate before publishing.
#
Pod::Spec.new do |s|
  s.name             = 'netease_meeting_ui'
  s.version          = '0.0.1'
  s.summary          = 'A new flutter plugin project.'
  s.description      = <<-DESC
A new flutter plugin project.
                       DESC
  s.homepage         = 'https://meeting.163.com'
  s.license          = { :file => '../LICENSE' }
  s.author           = { 'NetEase, Inc.' => 'hzsunyj@corp.netease.com' }
  s.source           = { :path => '.' }
  s.source_files = 'Classes/**/*'
  s.public_header_files = 'Classes/**/*.h'
  s.dependency 'Flutter'
  s.platform = :ios, '10.0'
  s.swift_version = '5.0'
  s.dependency 'YXAlog'
  # Flutter.framework does not contain a i386 slice. Only x86_64 simulators are supported.
  s.pod_target_xcconfig = { 'DEFINES_MODULE' => 'YES', 'VALID_ARCHS[sdk=iphonesimulator*]' => 'x86_64' }
end
