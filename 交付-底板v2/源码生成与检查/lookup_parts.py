import concurrent.futures
import json
from pathlib import Path
import requests

BASE = Path(__file__).parent
QUERIES = ['C57112','CC0603KRX7R9BB102','C25819','C22827','C25804','C22962',
           'C25803','C22809','PZ254V-11-08P','AO3401A','C347476','C20069125']

def run(q):
    p = BASE / (q+'.json')
    if p.exists():
        d = json.loads(p.read_text(encoding='utf-8'))
    else:
        r = requests.post('https://wmsc.lcsc.com/ftps/wm/search/v3/global',
            json={'keyword':q,'currentPage':1,'pageSize':6},
            headers={'User-Agent':'Mozilla/5.0','Origin':'https://www.lcsc.com',
                     'Referer':'https://www.lcsc.com/'}, timeout=35)
        r.raise_for_status(); d=r.json()
        p.write_text(json.dumps(d,ensure_ascii=False,indent=2),encoding='utf-8')
    result=d.get('result') or {}
    a=result.get('tipProductDetailUrlVO') or result.get('exactMatchResult')
    if not a:a=(result.get('productSearchResultVO') or {}).get('productList')
    print(q,result.get('scene'),json.dumps(a,ensure_ascii=False)[:1600])

if __name__ == '__main__':
    import sys
    if '--all' in sys.argv:
        QUERIES = sorted({p['code'] for p in json.loads((BASE/'parts.json').read_text(encoding='utf-8')) if p['code']})
    with concurrent.futures.ThreadPoolExecutor(max_workers=5) as pool:
        list(pool.map(run,QUERIES))
