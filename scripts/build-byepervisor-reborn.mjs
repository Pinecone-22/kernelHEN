import {spawnSync} from 'node:child_process';
import {mkdir,readFile,writeFile} from 'node:fs/promises';
import path from 'node:path';
import {root,source,sdk,zig,common,run} from './toolchain.mjs';

const dir=path.join(source,'bootstrapper/ByepervisorReborn');
const outDir=path.join(root,'build/byepervisor-reborn');
const asset=path.join(source,'bootstrapper/assets/byepervisor_reborn.elf');
await mkdir(outDir,{recursive:true});
await mkdir(path.dirname(asset),{recursive:true});

const breakSrc=path.join(dir,'break_hv_payload');
const breakElf=path.join(outDir,'break_hv.elf');
const breakBin=path.join(outDir,'break_hv.bin');
const breakObj=path.join(outDir,'break_hv.o');
const entryObj=path.join(outDir,'entry.o');
const breakFlags=['-target','x86_64-linux-none','-O2','-ffreestanding','-nostdlib','-fno-pic','-fno-plt',
 '-fno-stack-protector','-fno-stack-clash-protection','-fomit-frame-pointer','-fno-asynchronous-unwind-tables',
 '-fcf-protection=none','-fno-jump-tables','-mgeneral-regs-only','-mno-red-zone','-Wall','-Wextra'];

const cc=(args)=>spawnSync(zig,['cc',...args],{cwd:dir,encoding:'utf8',maxBuffer:16*1024**2});
let r=cc([...breakFlags,'-c',path.join(breakSrc,'break_hv.c'),'-o',breakObj]);
if(r.status!==0)throw Error((r.stdout||'')+(r.stderr||''));
r=cc(['-c',path.join(breakSrc,'entry.S'),'-o',entryObj]);
if(r.status!==0)throw Error((r.stdout||'')+(r.stderr||''));
r=spawnSync(zig,['ld.lld','-nostdlib','-no-pie','-T',path.join(breakSrc,'link.ld'),entryObj,breakObj,'-o',breakElf],
 {cwd:dir,encoding:'utf8',maxBuffer:16*1024**2});
if(r.status!==0)throw Error((r.stdout||'')+(r.stderr||''));
r=spawnSync(zig,['objcopy','-O','binary',breakElf,breakBin],{cwd:dir,encoding:'utf8',maxBuffer:16*1024**2});
if(r.status!==0)throw Error((r.stdout||'')+(r.stderr||''));

const payload=await readFile(breakBin);
const rows=[];
for(let i=0;i<payload.length;i+=16){
 rows.push('  '+[...payload.subarray(i,i+16)].map(x=>'0x'+x.toString(16).padStart(2,'0')).join(', '));
}
await writeFile(path.join(breakSrc,'break_hv.h'),
 `unsigned char break_hv[] = {\n${rows.join(',\n')}\n};\nunsigned int break_hv_len = ${payload.length};\n`);

const files=['main.c','kdlsym.c','khook.c','paging.c','util.c'];
const objects=[];
for(const file of files){
 const object=path.join(outDir,file+'.o');
 run(['cc',...common,'-I',path.join(dir,'include'),'-c',path.join(dir,file),'-o',object],{cwd:dir});
 objects.push(object);
}
run(['ld.lld','--no-dependent-libraries','-pie','--hash-style=gnu','-z','max-page-size=0x4000',
 '-T',path.join(sdk,'ldscripts/elf_x86_64.x'),...objects,path.join(sdk,'target/lib/crt1.o'),
 '-L',path.join(sdk,'target/lib'),'-L',path.join(source,'lib'),'--start-group',
 '-lkernel_web','-lSceLibcInternal','-lSceSystemService','-lSceNet','-lc','--end-group','-o',asset],{cwd:dir});
console.log('Built '+asset);
