// Builds the PS5 static SQLite archive used by the bootstrapper and daemon.
// Usage: set SQLITE_AMALGAMATION to the directory containing sqlite3.c
// (https://www.sqlite.org/download.html), then run node scripts/build-sqlite.mjs
import {mkdir,readFile,rm,stat} from 'node:fs/promises';
import path from 'node:path';
import {root,source,common,run} from './toolchain.mjs';

const dir=process.env.SQLITE_AMALGAMATION;
if(!dir)throw Error('Set SQLITE_AMALGAMATION to the extracted sqlite-amalgamation directory');
const amalgamation=path.resolve(dir);
await stat(path.join(amalgamation,'sqlite3.c'));
await stat(path.join(amalgamation,'sqlite3.h'));

const out=path.join(root,'dependencies/lib');
const object=path.join(root,'build/sqlite/sqlite3.o');
await mkdir(out,{recursive:true});
await mkdir(path.dirname(object),{recursive:true});

run(['cc',...common,'-O2','-I',amalgamation,'-I',path.join(source,'include'),
 '-DSQLITE_THREADSAFE=1','-DSQLITE_OMIT_LOAD_EXTENSION','-DSQLITE_OS_UNIX=1',
 '-c',path.join(amalgamation,'sqlite3.c'),'-o',object]);
const archivePath=path.join(out,'libsqlite3.a');
await rm(archivePath,{force:true});
run(['ar','rcs',archivePath,object]);
const archive=await readFile(archivePath);
console.log('Built '+archivePath+' ('+archive.length+' bytes)');
