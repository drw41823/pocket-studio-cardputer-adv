#pragma once
#include "SamplerPatch.h"
namespace pocket {
// FS supplies exact-length read/write, exists/remove/rename. A previous valid
// record remains in .bak until the next verified transaction can commit.
template<class FS> bool readSoundRecord(FS& fs,const char* path,SamplePatch& patch) {
    if(!fs.exists(path))return false; // Empty slots do not need four failing format probes.
    uint8_t bytes[SamplePatchBytes];
    if(fs.read(path,bytes,sizeof bytes))return decodeSamplePatch(bytes,sizeof bytes,patch);
    if(fs.read(path,bytes,ModSamplePatchBytes))return decodeSamplePatch(bytes,ModSamplePatchBytes,patch);
    if(fs.read(path,bytes,LoopSamplePatchBytes))return decodeSamplePatch(bytes,LoopSamplePatchBytes,patch);
    return fs.read(path,bytes,LegacySamplePatchBytes)&&decodeSamplePatch(bytes,LegacySamplePatchBytes,patch);
}
template<class FS> bool writeSoundRecord(FS& fs,const char* path,const char* temporary,const char* backup,const SamplePatch& patch) {
    if(!validPatch(patch))return false;
    uint8_t bytes[SamplePatchBytes],verify[SamplePatchBytes];encodeSamplePatch(patch,bytes);
    if(!fs.write(temporary,bytes,sizeof bytes)||!fs.read(temporary,verify,sizeof verify)||std::memcmp(bytes,verify,sizeof bytes))return false;
    SamplePatch previous;
    bool old=fs.exists(path)&&readSoundRecord(fs,path,previous);
    if(old) {
        if(fs.exists(backup)&&!fs.remove(backup))return false;
        if(!fs.rename(path,backup))return false;
    } else if(fs.exists(path)&&!fs.remove(path)) {
        return false; // Preserve any valid backup if the final was corrupt.
    }
    if(!fs.rename(temporary,path)) {
        if(old)fs.rename(backup,path);
        return false;
    }
    return true;
}
}
