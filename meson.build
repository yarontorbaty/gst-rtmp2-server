project('gst-rtmp2-server', 'c',
  version : '1.0.0',
  meson_version : '>= 0.60.0',
  default_options : [ 'warning_level=1',
                      'buildtype=debugoptimized' ])

gst_version = '>= 1.20.0'

gst_dep = dependency('gstreamer-1.0', version : gst_version,
    fallback : ['gstreamer', 'gst_dep'])
gstbase_dep = dependency('gstreamer-base-1.0', version : gst_version,
    fallback : ['gstreamer', 'gst_base_dep'])
gstapp_dep = dependency('gstreamer-app-1.0', version : gst_version,
    fallback : ['gstreamer', 'gst_app_dep'])
gstvideo_dep = dependency('gstreamer-video-1.0', version : gst_version,
    fallback : ['gstreamer', 'gst_video_dep'])
gstaudio_dep = dependency('gstreamer-audio-1.0', version : gst_version,
    fallback : ['gstreamer', 'gst_audio_dep'])

glib_dep = dependency('glib-2.0', version : '>= 2.56.0')
gio_dep = dependency('gio-2.0', version : '>= 2.56.0')

cc = meson.get_compiler('c')

plugin_install_dir = join_paths(get_option('libdir'), 'gstreamer-1.0')

cdata = configuration_data()
cdata.set_quoted('PACKAGE_VERSION', meson.project_version())
cdata.set_quoted('PACKAGE', 'gst-rtmp2-server')
cdata.set_quoted('GST_LICENSE', 'LGPL')
cdata.set_quoted('GST_API_VERSION', '1.0')
cdata.set_quoted('GST_PACKAGE_NAME', 'GStreamer RTMP2 Server Plugin')
cdata.set_quoted('GST_PACKAGE_ORIGIN', 'https://github.com/gstreamer/gst-plugins-bad')

configure_file(output : 'config.h', configuration : cdata)

# Shared RTMP library (from GStreamer gst-plugins-bad/gst/rtmp2/rtmp/)
rtmp_lib_sources = [
  'gst/rtmp/amf.c',
  'gst/rtmp/rtmpchunkstream.c',
  'gst/rtmp/rtmpclient.c',
  'gst/rtmp/rtmpconnection.c',
  'gst/rtmp/rtmphandshake.c',
  'gst/rtmp/rtmpmessage.c',
  'gst/rtmp/rtmputils.c',
  # New server-specific additions
  'gst/rtmp/rtmpserver.c',
  'gst/rtmp/rtmpflv.c',
]

# Plugin sources
gst_rtmp2_server_sources = [
  'gst/gstrtmp2serversrc.c',
  'gst/gstrtmp2server.c',
]

gst_rtmp2_server = library('gstrtmp2server',
  gst_rtmp2_server_sources + rtmp_lib_sources,
  include_directories : include_directories('.', 'gst'),
  c_args : ['-DHAVE_CONFIG_H'],
  dependencies : [gst_dep, gstbase_dep, gstapp_dep, gstvideo_dep, gstaudio_dep, glib_dep, gio_dep],
  install : true,
  install_dir : plugin_install_dir,
)
