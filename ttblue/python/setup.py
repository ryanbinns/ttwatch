#!/usr/bin/env python

from distutils.core import setup, Extension
setup(name = "crc16_modbus",
      version = "0.1",
      ext_modules=[
          Extension("crc16_modbus", ["crc16_modbus.c"], extra_compile_args=['--std=c99'])
      ],

      author = 'Dan Lenski',
      author_email = 'dlenski@gmail.com',
      license = 'GPLv3',
)
