from setuptools import Distribution, setup
from setuptools.command.bdist_wheel import bdist_wheel


class PlatformDistribution(Distribution):
    # Qt plugins are native ELF libraries, even though there is no CPython
    # extension. A wheel containing them must not be tagged py3-none-any.
    def has_ext_modules(self):
        return True


class QtPluginWheel(bdist_wheel):
    def get_tag(self):
        _,_,platform=super().get_tag()
        return 'py3','none',platform


setup(distclass=PlatformDistribution,cmdclass={'bdist_wheel':QtPluginWheel})
