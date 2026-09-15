#!/usr/bin/env python3
"""Verify fixed 0-120 C observer palettes and unchanged 20-120 C sensor PNGs.

python scripts/verify_thermal_ranges.py --directory artifacts/thermal-ranges --output artifacts/thermal-ranges/verification.json

Pillow only. Reads native GPU captures; never runs Unreal or model inference.
High-temperature surfaces are authored visual proxies, not calibrated welding
thermography. Pixel metrics do not establish real material temperatures.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import math
from pathlib import Path, PureWindowsPath

from PIL import Image, ImageChops, ImageOps, ImageStat

NAMES = ('white', 'black', 'iron', 'restored_white')
RAW_SIZE = (448, 256)
SCREEN_SIZE = (1600, 900)
OBSERVER_ROI = (550, 260, 1050, 660)
SCALE_ROI = (14, 746, 294, 828)
MAX_JSON_BYTES = 4_000_000
MAX_PNG_BYTES = 20_000_000


def finite(value):
    return type(value) in (float, int) and math.isfinite(value)


def finite_tree(value):
    if isinstance(value, dict):
        return all(finite_tree(v) for v in value.values())
    if isinstance(value, list):
        return all(finite_tree(v) for v in value)
    return finite(value) if type(value) in (float, int) else True


def near(a, b, tolerance=.0001):
    return finite(a) and abs(a-b) <= tolerance


def spread(image):
    r, g, b = image.split()
    return ImageChops.subtract(ImageChops.lighter(r, ImageChops.lighter(g, b)), ImageChops.darker(r, ImageChops.darker(g, b)))


def delta(first, second):
    d = ImageChops.difference(first.convert('RGB'), second.convert('RGB'))
    r, g, b = d.split()
    maximum = ImageChops.lighter(r, ImageChops.lighter(g, b))
    histogram = maximum.histogram()
    return {'maximum_dn': maximum.getextrema()[1], 'mean_absolute_dn': sum(ImageStat.Stat(d).mean)/3,
            'changed_pixels': sum(histogram[1:]), 'pixels': first.width*first.height}


def inverse_metrics(first, second):
    a, b = first.convert('L'), second.convert('L')
    sa, sb = ImageStat.Stat(a), ImageStat.Stat(b)
    ma, mb = sa.mean[0], sb.mean[0]
    denom = sa.stddev[0]*sb.stddev[0]
    covariance = sum((x-ma)*(y-mb) for x, y in zip(a.getdata(), b.getdata()))/(a.width*a.height)
    difference = ImageChops.difference(a, ImageOps.invert(b))
    histogram = difference.histogram(); goal=.99*a.width*a.height; count=0; p99=255
    for dn, frequency in enumerate(histogram):
        count += frequency
        if count >= goal:
            p99=dn; break
    return {'correlation': covariance/denom if denom else None, 'white_stddev': sa.stddev[0], 'black_stddev': sb.stddev[0],
            'inverse_mean_error_dn': ImageStat.Stat(difference).mean[0], 'inverse_p99_error_dn': p99}


def verify(directory):
    directory=Path(directory).resolve()
    result={'schema':'darkfactory-thermal-display-verification-v2', 'scope':'GPU observer range/palette isolation and fixed input parity; no model accuracy or physical calibration claim',
            'directory':str(directory), 'checks':{}, 'metrics':{}, 'errors':[]}
    checks=result['checks']; metrics=result['metrics']; cache={}; seen_paths=set()
    def check(name, condition):
        checks[name]=bool(condition)
    def png(name, size, grayscale=False):
        if not isinstance(name,str) or not name or Path(name).name!=name or PureWindowsPath(name).name!=name or PureWindowsPath(name).drive:
            raise ValueError('Expected a contained PNG basename')
        path=(directory/name).resolve()
        if path.parent!=directory or path.suffix.lower()!='.png' or not path.is_file() or not 0<path.stat().st_size<=MAX_PNG_BYTES:
            raise ValueError('Missing, outside or oversized PNG: '+name)
        if name not in cache:
            payload=path.read_bytes()
            with Image.open(path) as im:
                if im.format!='PNG' or im.size!=size or im.mode not in ('L','RGB','RGBA'):
                    raise ValueError('Unexpected PNG format/dimensions/mode: '+name)
                image=im.convert('RGB'); image.load()
            cache[name]=(image,hashlib.sha256(payload).hexdigest())
        image,digest=cache[name]
        if image.size!=size or grayscale and spread(image).getextrema()[1]!=0:
            raise ValueError('Unexpected dimensions or nongrayscale sensor PNG: '+name)
        seen_paths.add(name)
        return image,digest
    try:
        path=directory/'thermal_ranges.json'
        if path.stat().st_size>MAX_JSON_BYTES:
            raise ValueError('Native report exceeds bounded size')
        native=json.loads(path.read_text(encoding='utf-8-sig'),parse_constant=lambda x: (_ for _ in ()).throw(ValueError('Nonfinite JSON: '+x)))
        if not isinstance(native,dict) or not finite_tree(native):
            raise ValueError('Native report must be a finite object')
        check('native_schema',native.get('schema')=='darkfactory-thermal-display-smoke-v2')
        check('native_source',native.get('source')=='offline_gpu_display_qa')
        check('native_passed',native.get('passed') is True)
        check('no_model_requests',near(native.get('model_requests'),0))
        check('bounded_offline_capture_target',near(native.get('offline_only_additional_capture_targets'),1))
        check('no_added_interactive_capture_target',near(native.get('interactive_additional_capture_targets'),0))
        encoding=native.get('sensor_encoding',{})
        check('fixed_sensor_window_unchanged',isinstance(encoding,dict) and near(encoding.get('min_temperature_c'),20) and near(encoding.get('max_temperature_c'),120))
        check('observer_roi_matches_fixture',native.get('observer_roi')==list(OBSERVER_ROI))
        check('scale_roi_matches_fixture',native.get('observer_scale_roi')==list(SCALE_ROI))
        samples=native.get('samples')
        if not isinstance(samples,list) or len(samples)!=4 or [v.get('name') if isinstance(v,dict) else None for v in samples]!=list(NAMES):
            raise ValueError('Expected four ordered fixed-range palette states')
        check('exact_four_palette_sample_sequence',True)
        gpu={}; screens={}; hot=[]
        for i,row in enumerate(samples):
            name=NAMES[i]; palette=0 if i==3 else i
            check(name+'_palette',near(row.get('palette'),palette))
            check(name+'_window',near(row.get('min_c'),0) and near(row.get('max_c'),120))
            display=row.get('display',{})
            check(name+'_display_metadata',isinstance(display,dict) and near(display.get('min_temperature_c'),0)
                  and near(display.get('max_temperature_c'),120) and near(display.get('palette'),palette)
                  and display.get('range')=='fixed_observer' and display.get('schema')=='observer-thermal-fixed-v2'
                  and near(display.get('sensor_min_temperature_c'),20) and near(display.get('sensor_max_temperature_c'),120)
                  and display.get('sensor_range_changed') is False)
            check(name+'_hot_physical_surface',finite(row.get('hot_surface_c')) and 1400<row['hot_surface_c']<=1600.01)
            check(name+'_hot_apparent_temperature',finite(row.get('hot_apparent_c')) and 1000<row['hot_apparent_c']<1400)
            hot.append((row.get('hot_surface_c'),row.get('hot_apparent_c')))
            g,_=png(row.get('gpu_png'),RAW_SIZE); screen,_=png(row.get('screenshot'),SCREEN_SIZE)
            gpu[name]=g; screens[name]=screen
            roi=screen.crop(OBSERVER_ROI); st=ImageStat.Stat(roi.convert('L'))
            check(name+'_observer_scene_nonblank',st.stddev[0]>.5 and roi.convert('L').getextrema()[1]-roi.convert('L').getextrema()[0]>16)
            if palette<2:
                check(name+'_grayscale_gpu',spread(g).getextrema()[1]<=1)
            else:
                histogram=spread(g).histogram(); unique=len(g.getcolors(g.width*g.height) or [])
                metrics[name+'_iron']={'colored_pixels':sum(histogram[6:]),'distinct_colors':unique}
                check(name+'_iron_varied_color',sum(histogram[6:])>=8 and unique>=8)
        check('heat_frozen_across_palette_changes',all(finite(v) for pair in hot for v in pair) and max(p[0] for p in hot)-min(p[0] for p in hot)<.001 and max(p[1] for p in hot)-min(p[1] for p in hot)<.001)
        for key in ('fixed_raw_files','nir_raw_files'):
            names=native.get(key)
            if not isinstance(names,list) or len(names)!=4 or len(set(names))!=4:
                raise ValueError(key+' requires four distinct palette files')
            pictures=[png(name,RAW_SIZE,True) for name in names]
            check(key+'_exact_png_byte_parity',len({digest for _,digest in pictures})==1)
            if key=='fixed_raw_files':
                check('fixed_sensor_has_clipped_hot_pixels',pictures[0][0].getextrema()[0][1]==255)
                metrics['fixed_sensor_peak_dn']=pictures[0][0].getextrema()[0][1]
                fixed_picture=pictures[0][0]
        check('all_16_evidence_paths_distinct',len(seen_paths)==16)
        inv=inverse_metrics(gpu['white'],gpu['black']);metrics['white_black_inverse']=inv
        check('white_black_inverse',inv['correlation'] is not None and inv['correlation']<-.95 and inv['white_stddev']>.5 and inv['black_stddev']>.5 and inv['inverse_mean_error_dn']<=2 and inv['inverse_p99_error_dn']<=4)
        check('observer_hot_core_saturates_at_120',gpu['white'].getextrema()[0][1]==255)
        # The same un-clipped surface maps into different independent windows:
        # sensor DN = (T-20)/100*255; observer DN = T/120*255.
        # One DN of sensor quantization plus observer quantization fits inside
        # two DN. Saturated pixels cannot recover temperatures and are excluded.
        errors=[abs(o-(42.5+f*5/6)) for o,f in zip(gpu['white'].convert('L').getdata(),fixed_picture.convert('L').getdata()) if 2<=f<=252]
        metrics['independent_windows']={'eligible_pixels':len(errors),'maximum_error_dn':max(errors) if errors else None,'mean_error_dn':sum(errors)/len(errors) if errors else None}
        check('zero_based_observer_window_is_rendered',len(errors)>=100 and max(errors)<=2)
        check('restored_white_gpu_exact',delta(gpu['white'],gpu['restored_white'])['maximum_dn']==0)
        check('restored_white_observer_scene_exact',delta(screens['white'].crop(OBSERVER_ROI),screens['restored_white'].crop(OBSERVER_ROI))['maximum_dn']==0)
        scale=delta(screens['white'].crop(SCALE_ROI),screens['black'].crop(SCALE_ROI));metrics['scale_palette_change']=scale
        check('observer_scale_changes_with_palette',scale['changed_pixels']>=20 and scale['maximum_dn']>8)
        check('restored_white_observer_scale_exact',delta(screens['white'].crop(SCALE_ROI),screens['restored_white'].crop(SCALE_ROI))['maximum_dn']==0)
    except (OSError,ValueError,TypeError,KeyError,OverflowError) as exc:
        result['errors'].append(str(exc));check('complete_evidence_read',False)
    result['failed_checks']=[name for name,passed in checks.items() if not passed]
    result['checks_count']=len(checks)
    result['passed']=not result['errors'] and not result['failed_checks']
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory',type=Path,required=True)
    parser.add_argument('--output','--report',type=Path)
    args=parser.parse_args();result=verify(args.directory)
    if args.output:
        args.output.parent.mkdir(parents=True,exist_ok=True)
        args.output.write_text(json.dumps(result,indent=2,allow_nan=False)+'\n',encoding='utf-8')
    print(json.dumps(result,indent=2,allow_nan=False))
    return 0 if result['passed'] else 1


if __name__=='__main__':
    raise SystemExit(main())
