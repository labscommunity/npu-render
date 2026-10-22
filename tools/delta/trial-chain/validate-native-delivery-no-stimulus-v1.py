"""Reject frozen finite trials run without a color stimulus; this is not a desktop responsiveness gate."""
import argparse
from datetime import UTC,datetime
import json
from pathlib import Path
import traceback

def obj(path):
    data=path.read_bytes()
    return json.loads(data.decode('utf-16') if data.startswith(b'\xff\xfe') else data.decode('utf-8-sig'))

def rows(path):
    return [json.loads(s) for s in path.read_text(encoding='utf-8-sig').splitlines() if s.startswith('{') and s.endswith('}')]

def validate(trial):
    live=obj(trial/'report.json');relay=obj(trial/'relay/report.json')
    present=rows(trial/'present/probe.log');screen=rows(trial/'screen/probe.log')
    published=rows(trial/'relay/frames.jsonl')
    assert relay['completed'] and relay['passed'] and not relay.get('error'),'relay did not complete successfully'
    assert relay['published']==len(published),'relay publication count mismatch'
    done=[x for x in present if x.get('live_bridge_tested') and x.get('completed')]
    exits=[x['exit_code'] for x in present if 'exit_code' in x]
    assert len(done)==1 and exits==[0],'presenter did not complete successfully'
    frames=[x for x in present if x.get('live_frame_presented')]
    assert len(frames)>=3 and done[0]['submitted_presents']==len(frames),'presentation count mismatch'
    hz=frames[0]['qpc_frequency'];assert hz>0 and all(f['qpc_frequency']==hz for f in frames)
    assert all(b['accepted_qpc']>a['accepted_qpc'] and b['source_qpc']>a['source_qpc'] and b['source_sequence']>a['source_sequence'] for a,b in zip(frames,frames[1:])), 'nonmonotonic presentation'
    for f in frames:
        assert f['backbuffer_exact'],'inexact backbuffer'
        assert sum(p['sequence']==f['source_sequence'] and p['qpc']==f['source_qpc'] and p['producer_pid']==f['producer_pid'] for p in published)==1,'source/presentation identity mismatch'
    assert len({p['pixel_sha256'] for p in published})>=3,'changing pixels absent'
    elapsed=done[0]['elapsed_ms']/1000
    assert elapsed>=live['presentation_seconds'] and elapsed<=live['presentation_seconds']+15,'requested duration not observed'
    observations=[x for x in present if x.get('display_observation')]
    assert observations,'end-of-trial display clock absent'
    end=observations[-1]['observed_qpc']
    span=(frames[-1]['accepted_qpc']-frames[0]['accepted_qpc'])/hz
    age=(end-frames[-1]['accepted_qpc'])/hz
    # A minute is a coarse liveness bound for this slow diagnostic, not usable
    # desktop latency. The product's 30 fps / 100 ms target remains separate.
    assert span>=elapsed-60,'delivery ceased before the requested interval ended'
    assert 0<=age<=60,'last delivered frame is stale'
    screen_done=[x for x in screen if x.get('physical_output_observer') and x.get('completed')]
    assert len(screen_done)==1,'physical observer did not complete'
    fresh=[x for x in screen if x.get('qpc',0)>=end-300*hz and x.get('last_present_qpc',0)>=end-300*hz and x.get('ownership_verified_before_and_after') and len(x.get('points',[]))==2 and all(p.get('owned_presenter_at_point') for p in x['points'])]
    assert fresh,'fresh independently observed owned presenter absent'
    return dict(passed=True,matching_submissions=len(frames),first_to_last_seconds=span,last_delivery_age_seconds=age,fresh_physical_observations=len(fresh),stimulus_colors_required=False,physical_observer_passed=bool(screen_done[0].get('passed')),physical_observer_exit_codes=[x['exit_code'] for x in screen if 'exit_code' in x],liveness_bound_seconds=60,responsiveness_qualified=False,full_desktop_offload=False,tahoma_tested=False)

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--trial',type=Path,required=True);p.add_argument('--output',type=Path,required=True);v=p.parse_args()
    report=dict(started=datetime.now(UTC).isoformat(),completed=False,passed=False)
    try:report.update(validate(v.trial))
    except Exception as e:report.update(error=repr(e),traceback=traceback.format_exc())
    report.update(completed=True,finished=datetime.now(UTC).isoformat())
    v.output.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report));raise SystemExit(0 if report['passed'] else 1)
