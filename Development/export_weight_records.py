#!/usr/bin/env python3
import argparse, json, pathlib

def main():
    p=argparse.ArgumentParser()
    p.add_argument("dll",type=pathlib.Path);p.add_argument("index",type=pathlib.Path)
    p.add_argument("output",type=pathlib.Path);p.add_argument("names",nargs="+")
    a=p.parse_args();data=a.dll.read_bytes();records=json.loads(a.index.read_text())
    resource_file_offset=0x114A160
    by_name={r["name"]:r for r in records};a.output.mkdir(parents=True,exist_ok=True)
    for name in a.names:
        r=by_name[name];start=resource_file_offset+r["payload_offset"];payload=data[start:start+r["payload_size"]]
        path=a.output/(name.replace(".","-")+".weights");path.write_bytes(payload)
        print(f"{name} {len(payload)} {path}")
if __name__=="__main__":main()
