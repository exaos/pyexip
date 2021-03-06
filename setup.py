from distutils.core import setup
from distutils.extension import Extension
from Cython.Distutils import build_ext
from Cython.Build import cythonize

#from distutils.command.install import install as DistutilsInstall
import subprocess
import os.path
import os
import shutil

class BuildExip(build_ext):
    def run(self):
        os.chdir(os.path.join("exip-0.4.1", "build", "gcc"))
        subprocess.call("make lib",  shell=True)
        os.chdir("../../../")
        build_ext.run(self)

src_dir = "src"
sources = ["pyexip.pyx"]

sources_with_path = [ os.path.join(src_dir, source) for source in sources ]


setup(
    name="pyexip",
    version="0.1.0",
    description="An EXI (Efficient XML Interchange) solution for Python based on EXIP.",
    author="Salman Arshad",
    cmdclass = {'build_ext': BuildExip},
    ext_modules = [Extension("pyexip", 
                            sources_with_path, 
                            extra_objects=[os.path.join("exip-0.4.1", "bin", "lib", "libexip.a")]
                            )]
)