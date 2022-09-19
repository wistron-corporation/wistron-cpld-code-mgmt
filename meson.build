project('wistron-cpld-code-mgmt', 'cpp',
    default_options: [
        'buildtype=debugoptimized',
        'cpp_std=c++20',
        'warning_level=3',
        'werror=true'
    ],
    meson_version: '>= 0.57.0',
    license: 'Apache-2.0',
    version: '1.0')

add_project_arguments(
    '-DBOOST_SYSTEM_NO_DEPRECATED',
    '-DBOOST_ERROR_CODE_HEADER_ONLY',
    '-DBOOST_NO_RTTI',
    '-DBOOST_NO_TYPEID',
    '-DBOOST_ALL_NO_LIB',
    '-DBOOST_ASIO_DISABLE_THREADS',
    '-DBOOST_ASIO_NO_DEPRECATED',
    language: 'cpp',
)

cpp = meson.get_compiler('cpp')

conf = configuration_data()

# DBus information
conf.set_quoted('CPLD_INVENTORY_INTERFACE', 'xyz.openbmc_project.Inventory.Item.Cpld')
conf.set_quoted('CPLD_INVENTORY_PATH', '/xyz/openbmc_project/inventory/system/chassis')
conf.set_quoted('BUSNAME_UPDATER', 'xyz.openbmc_project.Software.CPLD.Updater')
conf.set_quoted('CHASSIS_STATE_OBJ', 'xyz.openbmc_project.State.Chassis')
conf.set_quoted('CHASSIS_STATE_OFF', 'xyz.openbmc_project.State.Chassis.PowerState.Off')
conf.set_quoted('CHASSIS_STATE_PATH', '/xyz/openbmc_project/state/chassis0')
conf.set_quoted('FILEPATH_IFACE', 'xyz.openbmc_project.Common.FilePath')
conf.set_quoted('INVENTORY_PATH', '/xyz/openbmc_project/inventory/')
conf.set_quoted('MAPPER_BUSNAME', 'xyz.openbmc_project.ObjectMapper')
conf.set_quoted('MAPPER_INTERFACE', 'xyz.openbmc_project.ObjectMapper')
conf.set_quoted('MAPPER_PATH', '/xyz/openbmc_project/object_mapper')
conf.set_quoted('SOFTWARE_OBJPATH', '/xyz/openbmc_project/software')
conf.set_quoted('SYSTEMD_BUSNAME', 'org.freedesktop.systemd1')
conf.set_quoted('SYSTEMD_PATH', '/org/freedesktop/systemd1')
conf.set_quoted('SYSTEMD_INTERFACE', 'org.freedesktop.systemd1.Manager')
conf.set_quoted('SYSTEMD_PROPERTY_INTERFACE', 'org.freedesktop.DBus.Properties')
conf.set_quoted('VERSION_BUSNAME', 'xyz.openbmc_project.Software.Version')
conf.set_quoted('VERSION_IFACE', 'xyz.openbmc_project.Software.Version')
conf.set_quoted('EXTENDED_VERSION_IFACE', 'xyz.openbmc_project.Software.ExtendedVersion')

# Names of the forward and reverse associations
conf.set_quoted('ACTIVATION_FWD_ASSOCIATION', 'inventory')
conf.set_quoted('ACTIVATION_REV_ASSOCIATION', 'activation')
conf.set_quoted('ACTIVE_FWD_ASSOCIATION', 'active')
conf.set_quoted('ACTIVE_REV_ASSOCIATION', 'software_version')
conf.set_quoted('FUNCTIONAL_FWD_ASSOCIATION', 'functional')
conf.set_quoted('FUNCTIONAL_REV_ASSOCIATION', 'software_version')
conf.set_quoted('UPDATEABLE_FWD_ASSOCIATION', 'updateable')
conf.set_quoted('UPDATEABLE_REV_ASSOCIATION', 'software_version')

# Filesystem files and directories
# The prefix path for the versioned cpld
conf.set_quoted('CPLD_SVF_PREFIX', get_option('media-dir') + '/cpld-')
# The name of the CPLD table of contents file
conf.set_quoted('CPLD_RELEASE_FILE', '/etc/cpld-release')
conf.set_quoted('CPLD_RELEASE_FILE_NAME', 'cpld-release')
# The dir where activation data is stored in files
conf.set_quoted('PERSIST_DIR', '/var/lib/wistron-cpld-code-mgmt/')
conf.set_quoted('CPLD_ACTIVE_DIR', '/var/lib/wistron-cpld-code-mgmt/cpld')


# Configurable variables
conf.set('ACTIVE_CPLD_MAX_ALLOWED', get_option('active-cpld-max-allowed'))
conf.set_quoted('SVF_UPLOAD_DIR', get_option('img-upload-dir'))
conf.set_quoted('MANIFEST_FILE_NAME', get_option('manifest-file-name'))
conf.set_quoted('MEDIA_DIR', get_option('media-dir'))

configure_file(output: 'config.h', configuration: conf)

sdbusplus_dep = dependency('sdbusplus', required: false)
sdbusplus_proj = dependency('', required: false)
if not sdbusplus_dep.found() or sdbusplus_dep.type_name() == 'internal'
    sdbusplus_proj = subproject('sdbusplus')
endif
if not sdbusplus_dep.found()
    sdbusplus_dep = sdbusplus_proj.get_variable('sdbusplus_dep')
endif

if sdbusplus_proj.found()
    sdbusplusplus_prog = sdbusplus_proj.get_variable('sdbusplusplus_prog')
    sdbuspp_gen_meson_prog = sdbusplus_proj.get_variable(
        'sdbuspp_gen_meson_prog')
else
    sdbusplusplus_prog = find_program('sdbus++', native: true)
    sdbuspp_gen_meson_prog = find_program('sdbus++-gen-meson', native: true)
endif

cereal_dep = dependency('cereal', required: false)
has_cereal = cpp.has_header_symbol(
    'cereal/cereal.hpp',
    'cereal::specialize',
    dependencies: cereal_dep,
    required: false)
if not has_cereal
    cereal_opts = import('cmake').subproject_options()
    cereal_opts.add_cmake_defines({'BUILD_TESTS': 'OFF'})
    cereal_proj = import('cmake').subproject(
        'cereal',
        options: cereal_opts,
        required: false)
    assert(cereal_proj.found(), 'cereal is required')
    cereal_dep = cereal_proj.dependency('cereal')
endif

deps = [
    dependency(
        'phosphor-dbus-interfaces',
        fallback: ['phosphor-dbus-interfaces', 'phosphor_dbus_interfaces_dep'],
    ),
    dependency(
        'phosphor-logging',
        fallback: ['phosphor-logging', 'phosphor_logging_dep'],
    ),
    sdbusplus_dep,
    cereal_dep,
]

ssl = dependency('openssl')

systemd = dependency('systemd')
systemd_system_unit_dir = systemd.get_pkgconfig_variable('systemdsystemunitdir')

unit_files = [
    'xyz.openbmc_project.Software.CPLD.Updater.service.in',
    'obmc-cpld-update-fw@.service.in',
    'obmc-cpld-update-init.service.in'
]

subdir('xyz/openbmc_project/Software/Image')

executable(
    'wistron-cpld-updater',
    image_error_cpp,
    image_error_hpp,
    'activation.cpp',
    'item_updater.cpp',
    'item_updater_main.cpp',
    'serialize.cpp',
    'version.cpp',
    'utils.cpp',
    'watch.cpp',
    dependencies: [deps, ssl, dependency('sdeventplus')],
    install: true
)

install_data('obmc-cpld-update',
    install_mode: 'rwxr-xr-x',
    install_dir: get_option('bindir')
)

foreach u : unit_files
    configure_file(
        input: u,
        output: '@BASENAME@',
        configuration: conf,
        install: true,
        install_dir: systemd_system_unit_dir,
    )
endforeach
