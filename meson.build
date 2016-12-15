project(
    'boinc-ipc-manager',
    'cpp',
    default_options: [
        'cpp_std=c++14',
    ],
)

executable(
    'boinc-ipc-manager',
    [
        'src/main.cpp',
    ],
    dependencies: [
        dependency('boinc-app-api'),
        dependency('jsoncpp'),
        dependency('libxml++-3.0'),
        dependency('threads'),
    ],
    install: true,
)
