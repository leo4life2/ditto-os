"""Keep validation evidence attached to the currently selected asset."""
import json,re,os

def evidence_directory(root):
    asset=json.loads((root/'assets/avatar-manifest.json').read_text())['id']
    override=os.environ.get('SCOUT_EVIDENCE_NAME')
    stem=override or asset.removesuffix('-prototype').removesuffix('-engineering')
    if not re.fullmatch(r'[a-z0-9][a-z0-9._-]*',stem):raise ValueError('Invalid avatar ID')
    out=root/'test-results'/(stem if override else stem+'-integration')
    out.mkdir(parents=True,exist_ok=True)
    return out
