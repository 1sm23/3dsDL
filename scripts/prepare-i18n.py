#!/usr/bin/env python3
"""Prepare translation drafts; runtime localization is fully offline."""
import concurrent.futures
import json
import pathlib
import re
import time
import urllib.parse
import urllib.request

PATH = pathlib.Path('romfs/i18n.json')
catalog = json.loads(PATH.read_text())
codes = {'zh': 'zh-CN', 'es': 'es', 'fr': 'fr', 'pt': 'pt', 'de': 'de', 'ja': 'ja'}

def translate_batch(code, items):
    protected = []
    for key, english in items:
        value = re.sub(r'\{(\d+)\}', lambda m: 'XQZ' + m[1] + 'ZQX', english.strip())
        protected.append(value)
    params = [('client', 'dict-chrome-ex'), ('sl', 'en'), ('tl', codes[code])]
    params += [('q', text) for text in protected]
    url = 'https://translate.googleapis.com/translate_a/t?' + urllib.parse.urlencode(params)
    for attempt in range(5):
        try:
            with urllib.request.urlopen(url, timeout=45) as response:
                values = json.load(response)
            if not isinstance(values, list) or len(values) != len(items):
                raise ValueError('Unexpected translation response')
            result = []
            for (key, english), translated in zip(items, values):
                translated = re.sub(r'XQZ\s*(\d+)\s*ZQX', lambda m: '{' + m[1] + '}', translated, flags=re.I)
                if sorted(re.findall(r'\{\d+\}', translated)) != sorted(re.findall(r'\{\d+\}', english)):
                    raise ValueError('Invalid placeholders for ' + key)
                if not translated.strip():
                    raise ValueError('Empty translation for ' + key)
                translated = english[:len(english) - len(english.lstrip())] + translated.strip() + english[len(english.rstrip()):]
                result.append((key, code, translated))
            return result
        except Exception:
            if attempt == 4:
                if len(items) > 1:
                    return [value for item in items for value in translate_batch(code, [item])]
                raise
            time.sleep(2 ** attempt)

jobs = []
for code in codes:
    batch = []
    budget = 0
    for key, values in catalog.items():
        if code in values:
            continue
        english = values['en']
        size = len(urllib.parse.quote(english))
        if batch and (budget + size > 3500 or len(batch) >= 24):
            jobs.append((code, batch))
            batch, budget = [], 0
        batch.append((key, english))
        budget += size
    if batch:
        jobs.append((code, batch))

with concurrent.futures.ThreadPoolExecutor(max_workers=6) as pool:
    pending = [pool.submit(translate_batch, *job) for job in jobs]
    for number, future in enumerate(concurrent.futures.as_completed(pending), 1):
        for key, code, text in future.result():
            catalog[key][code] = text
        PATH.write_text(json.dumps(catalog,ensure_ascii=False,indent=2) + '\n')
        print(f'Completed batch {number}/{len(jobs)}', flush=True)
