from setuptools import setup, find_packages

setup(
    name='stratatools',
    version='3.1',
    description='A library to interact with Stratasys cartridge material.',
    #long_description=long_description,
    url='https://github.com/bvanheu/stratatools',
    author='benjamin vanheuverzwijn',
    author_email='bvanheu@gmail.com',
    classifiers=[
        'Development Status :: 4 - Beta',
        'Intended Audience :: Developers',
        'License :: OSI Approved :: MIT License',
        'Programming Language :: Python :: 3',
        'Programming Language :: Python :: 3.6',
        'Programming Language :: Python :: 3.7',
        'Programming Language :: Python :: 3.8',
        'Programming Language :: Python :: 3.9',
        'Programming Language :: Python :: 3.10',
    ],
    keywords='stratasys 3dprinting',
    packages=find_packages(exclude=['contrib', 'docs', 'tests']),
    install_requires=[
        'pycryptodome',
        'pyserial',
        'protobuf',
        'PyQt5>=5.15.0',
    ],
    extras_require={
        'testing': ['pytest'],
        'rpi': ['pyudev'],  # Optional for Raspberry Pi daemon
    },
    test_suite='stratatools',
    entry_points={
        'console_scripts': [
            'stratatools=stratatools.console_app:main',
            'stratatools_gui=stratatools_gui:main',
            'stratatools_bp_read=stratatools.helper.bp_read:main',
            'stratatools_bp_write=stratatools.helper.bp_write:main',
            'stratatools_rpi_daemon=stratatools.helper.rpi_daemon:main',
            'stratatools_esp32_read=stratatools.helper.esp32_read:main',
            'stratatools_esp32_write=stratatools.helper.esp32_write:main',
        ],
    },
)
