"""Run exported firmware's offline C++ tests. No robot or private files needed."""
from pathlib import Path
import os, shutil, subprocess, sys
root=Path(__file__).resolve().parents[1]
fw=root/'firmware'
compiler=os.environ.get('CXX','g++')
if not shutil.which(compiler) and not Path(compiler).is_file():
    sys.exit('Install a C++17 compiler or set CXX to its executable.')
build=Path(os.environ.get('GLADIATOR_HOST_BUILD_DIR',str(root/'.build/host'))); build.mkdir(parents=True,exist_ok=True)
env=os.environ.copy(); env['PATH']=str(Path(shutil.which(compiler) or compiler).parent)+os.pathsep+env.get('PATH','')
cases=[
 ('tests/orientation_test.cpp',[]),
 ('tests/drive_power_test.cpp',[]),
 ('tests/visual_state_test.cpp',[]),
 ('tests/mast_policy_test.cpp',[]),
 ('tests/operator_status_test.cpp',[]),
 ('tests/json_stream_test.cpp',[]),
 ('tests/range_grid_test.cpp',['-Ishared']),
 ('tests/mast_hardware_test.cpp',['-Itests/mast_gpio_mocks']),
 ('tests/i2c_bus_recovery_test.cpp',['-Itests/i2c_mocks','-Isrc/sensors','-Isrc']),
 ('test/host/test_protocol.cpp',['-DGLADIATOR_PROTOCOL_TEST=1']),
 ('test/host/test_link.cpp',[]),
 ('test/host/test_trim.cpp',[]),
]
if os.name=='nt': cases.append(('test/host/test_wifi.cpp',['-Itest/host','-lbcrypt']))
prefix=[compiler,'c++'] if Path(compiler).stem=='zig' else [compiler]
for source,flags in cases:
    executable=build/(Path(source).stem+('.exe' if os.name=='nt' else ''))
    subprocess.run([*prefix,'-std=c++17','-O2',*[f for f in flags if not f.startswith('-l')],'-Itest/host',source,'-o',str(executable),*[f for f in flags if f.startswith('-l')]],cwd=fw,check=True,env=env)
    subprocess.run([str(executable)],cwd=fw,check=True,env=env,timeout=90)
print(str(len(cases))+' C++ host suites passed.')
