
libcamera_user_tests = [
    {'name': 'bayer_format', 'sources': ['bayer_format.cpp']},
]

foreach libcamera_user_test : libcamera_user_tests 
    exe = executable(libcamera_user_test['name'], libcamera_user_test['sources'],
                        dependencies : libcamera_private,
                        link_with : test_libraries,
                        include_directories : test_includes_internal)
    test(test['name'], exe, suite: 'user')
endforeach

user_tests = [
    {'name': 'video_device', 'sources': ['video_device.cpp']},
]

foreach user_test : user_tests 
    exe = executable(user_test['name'], user_test['sources'])
    test(test['name'], exe, suite: 'user')
endforeach

