"""Prepare an isolated, sealed-source S3 span probe. Does not flash or open USB."""
from pathlib import Path
import argparse,json,shlex,shutil
from build_provenance import ROOT,verify

def prepare(output):
    seal=verify(ROOT)
    output=output.resolve()
    folders=('main','components','managed_components','assets','host','tools','tests','examples')
    if any(output.is_relative_to(ROOT/name) for name in folders):
        raise ValueError('Place the probe outside copied source folders, for example under test-results')
    output.mkdir(parents=True,exist_ok=False)
    for name in ('main','components','managed_components','assets','host','tools','tests','examples'):
        shutil.copytree(ROOT/name,output/name,ignore=shutil.ignore_patterns('__pycache__','*.pyc'))
    for name in ('CMakeLists.txt','VERSION','build.sh','sdkconfig','sdkconfig.defaults','partitions.csv','dependencies.lock'):
        shutil.copyfile(ROOT/name,output/name)
    for src,dst in [('surface_span_s3_probe.cpp','span_diagnostic.cpp'),('surface_span_reference.h','surface_span_reference.h')]:
        shutil.copyfile(ROOT/'tests'/src,output/'main'/dst)
    cmake=output/'main/CMakeLists.txt'
    cmake.write_text(cmake.read_text()+'\ntarget_sources(${COMPONENT_LIB} PRIVATE "span_diagnostic.cpp")\n')
    services=output/'main/services.cpp';s=services.read_text()
    marker='  } else if (!strcmp(tool, "debug.power")) {'
    if s.count(marker)!=1:raise ValueError('Diagnostic insertion point changed; review the generated copy')
    s='extern "C" void scout_span_diagnostic();\n'+s
    s=s.replace(marker,'  } else if (!strcmp(tool, "debug.span.check")) {\n    scout_span_diagnostic();\n'+marker)
    services.write_text(s)
    (output/'probe-source.json').write_text(json.dumps({'source_version':seal['version'],'source_app_sha256':seal['artifacts']['build/scout_os.bin'],'scope':'Temporary hardware diagnostic copy; no USB operation performed'},indent=2)+'\n')
    # Keep the default SDK location of the original project when building its copy.
    env=['env','IDF_PATH='+str(ROOT.parent/'esp-idf'),'IDF_TOOLS_PATH='+str(ROOT.parent/'idf-tools')]
    print('Build: '+shlex.join([*env,'bash',str(output/'build.sh'),'build']))
    print('After stopping the bridge, flash with the same command ending in -p PORT app-flash.')
    print('Then run tests/surface_span_hardware_check.py --project '+shlex.quote(str(output))+' --port PORT --output FRESH.json')
    return output

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('output',type=Path)
    a=p.parse_args();prepare(a.output)
