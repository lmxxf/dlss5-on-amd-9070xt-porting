from pathlib import Path
import subprocess,shutil,difflib,json
root=Path(__file__).resolve().parents[3];here=Path(__file__).resolve().parent
host=Path(__import__('os').environ.get('RE9_UPSTREAM','/tmp/re9-upstream-bridge-review'));rev='8f71f73bfc836a37936e7cee6701750ad4e8bfec'
assert subprocess.check_output(['git','rev-parse','HEAD'],cwd=host,text=True).strip()==rev
prefix='OptiScaler-DLSSNR-PreSR-Multipass-main/OptiScaler/'
def original(name):return subprocess.check_output(['git','show',rev+':'+prefix+name],cwd=host,text=True)
def write(name,s):
 old=original(name);(host/prefix/name).write_text(s)
 (here/(Path(name).name+'.patch')).write_text(''.join(difflib.unified_diff(old.splitlines(True),s.splitlines(True),fromfile='a/'+prefix+name,tofile='b/'+prefix+name,n=2)))
name='dlssnr/backend/lmxxf_runtime/LmxxfNrApi.h';s=original(name).replace('#define LMXXF_NR_ABI_VERSION 1u','#define LMXXF_NR_ABI_VERSION 2u')
s=s.replace('    float model_scale;       /* 0.25..1.0, default 1.0 */','    float model_scale;       /* 0.25..1.0, default 1.0 */\n    void *exposure;\n    uint32_t exposure_state;\n    float pre_exposure, exposure_scale;')
write(name,s)
name='dlssnr/backend/lmxxf_runtime/LmxxfNrRuntime.cpp';s=original(name)
s=s.replace('(info->struct_size != sizeof(LmxxfNrFrameInfo) && info->struct_size != legacySize)', '(info->struct_size != sizeof(LmxxfNrFrameInfo))')
s=s.replace('#include "hip_d3d12_bridge.h"','#include "hip_d3d12_bridge.h"\n#include "ColourCapture.h"')
s=s.replace('    Job job {};','    Job job {};\n    Re9ColourCapture capture;\n    ID3D12Resource* exposure=nullptr;\n    D3D12_RESOURCE_STATES exposureState=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;')
s=s.replace('        session->job = {};\n        session->job.color = color;', '        session->capture.Begin(DllDirectory(),info->frame_id,info->pre_exposure,info->exposure_scale,(ID3D12Resource*)info->exposure);\n        session->exposure=(ID3D12Resource*)info->exposure;\n        session->exposureState=(D3D12_RESOURCE_STATES)info->exposure_state;\n        session->job = {};\n        session->job.color = color;')
s=s.replace('        session->encode->Record(list, {j->colorState}, 1.f);', '        session->capture.Record(session->device,list,j->color,j->colorState,"input");\n        session->capture.Record(session->device,list,session->exposure,session->exposureState,"exposure");\n        session->encode->Record(list, {j->colorState}, 1.f);\n        session->capture.Record(session->device,list,session->encode->Output(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,"proxy");')
s=s.replace('        j->state = LMXXF_NR_JOB_CONSUMER_COMPLETE;', '        session->capture.Record(session->device,list,session->rgbTex->Output(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,"neural");\n        session->capture.Record(session->device,list,session->decode->Output(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,"result");\n        j->state = LMXXF_NR_JOB_CONSUMER_COMPLETE;')

s=s.replace('JoinPath(dll, L"shaders"),','JoinPath(dll, L"DLSS5-AMD\\\\native-game-tiled-assets"),\n        JoinPath(dll, L"..\\\\DLSS5-AMD\\\\native-game-tiled-assets"),\n        JoinPath(dll, L"shaders"),')
s=s.replace('session->bridge->EnqueueAfterProducer(j->seed, false);','session->bridge->EnqueueAfterProducer(session->queue, j->seed, false);')
s=s.replace('1.f, j->transfer_strength, j->color_strength, j->debug_view);','1.f, NativeCodecParameters{j->transfer_strength, j->color_strength, NativeCodecDebugView(j->debug_view)});')
s=s.replace('    uint32_t state = LMXXF_NR_JOB_NONE;', '    uint32_t state = LMXXF_NR_JOB_NONE;\n    bool hipQueued = false, outputsRecorded = false;')
s=s.replace('        j->state = LMXXF_NR_JOB_NR_COMPLETE;', '        j->hipQueued = true;\n        j->state = LMXXF_NR_JOB_NR_COMPLETE;')
s=s.replace('        j->state = LMXXF_NR_JOB_CONSUMER_COMPLETE;', '        j->outputsRecorded = true;\n        j->state = LMXXF_NR_JOB_CONSUMER_COMPLETE;')
s=s.replace('        if (j)\n            j->state = LMXXF_NR_JOB_RETIRED;', '        if (!j || !j->hipQueued || !j->outputsRecorded)\n            return Fail(LMXXF_NR_INVALID_ARGUMENT, "Retire: producer/HIP/consumer incomplete");\n        session->bridge->NotifyOutputSubmitted(session->queue);\n        j->state = LMXXF_NR_JOB_RETIRED;')
s=s.replace('        if (FAILED(DrainGpu()))','        if ((bridge && !bridge->WaitForSubmittedWork()) || FAILED(DrainGpu()))')
# Establish independent CRT values for the actual linear pre-SR input. Do not reuse post-present sRGB mode.
s=s.replace('        NativeResolveNetworkGeometry(info->color_width, info->color_height);','        _wputenv_s(L"DLSS5_CODEC_SRGB", L"0");\n        NativeResolveNetworkGeometry(info->color_width, info->color_height);')
s=s.replace('enc->Create(session->device, {color}, session->shaderDir);', 'enc->Create(session->device, {color}, session->shaderDir, true);')
s=s.replace('dec->Create(session->device, {enc->Output(), rgbOut->Output(), color}, session->shaderDir);', 'dec->Create(session->device, {enc->Output(), rgbOut->Output(), color}, session->shaderDir, true);')
s=s.replace('        j->state = LMXXF_NR_JOB_RETIRED;', '        if(session->capture.active){if(FAILED(session->DrainGpu()))return Fail(LMXXF_NR_FAILED, "capture completion failed");session->capture.DumpAfterCompletion();}\n        j->state = LMXXF_NR_JOB_RETIRED;')
s=s.replace('    float color_strength = 1.0f;','    float color_strength = 1.0f;\n    float pre_exposure=1.f,exposure_scale=1.f;',1)
s=s.replace('    void TeardownCodecChain()', '    std::vector<D3D12_RESOURCE_STATES> CodecStates(std::vector<D3D12_RESOURCE_STATES> s){if(exposure)s.push_back(exposureState);return s;}\n    void TeardownCodecChain()')
s=s.replace('            (session->job.width != info->color_width', '            (session->exposure != info->exposure || session->job.width != info->color_width')
s=s.replace('session->shaderDir, true);', 'session->shaderDir, true, (ID3D12Resource*)info->exposure);')
s=s.replace('        session->job.color_strength = color_strength;', '        session->job.color_strength = color_strength;\n        session->job.pre_exposure=std::isfinite(info->pre_exposure)&&info->pre_exposure>0?info->pre_exposure:1.f;\n        session->job.exposure_scale=std::isfinite(info->exposure_scale)&&info->exposure_scale>0?info->exposure_scale:1.f;')
s=s.replace('session->encode->Record(list, {j->colorState}, 1.f);', 'session->encode->Record(list, session->CodecStates({j->colorState}), 1.f, NativeCodecParameters{1.f,1.f,NativeCodecDebugView::Final,j->pre_exposure,j->exposure_scale});')
s=s.replace('{D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,\n                                 D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, j->colorState},', 'session->CodecStates({D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,\n                                 D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, j->colorState}),')
s=s.replace('NativeCodecDebugView(j->debug_view)}', 'NativeCodecDebugView(j->debug_view),j->pre_exposure,j->exposure_scale}')
s=s.replace('session->bridge->Create(session->queue, opt, {});', 'session->bridge->Create(session->queue, opt, {});\n            session->bridge->PrepareStagedKernels();')
# Reject unsupported render inputs before changing network globals or allocating HIP.
# TheAutomatic supplies the host submission design; this transaction is our runtime adaptation.
start=s.index('        if (!session->hipPrepared)\n', s.index('int32_t PrepareFrame('))
end=s.index('        auto *color =', start)
s=s[:start]+s[end:]
start=s.index('        auto *color =', s.index('int32_t PrepareFrame('))
end=s.index('        const bool geoChanged', start)
validation=s[start:end]
s=s[:start]+s[end:]
validation += r'''        {
            // DLSS5_FIT_LARGE=1 in DLSS5-AMD\native-game-flags.txt (issue #6): inputs beyond 1920x1080 are fitted (downsampled)
            // onto the network surface by the codec and restored to the source extent, like small inputs since 0.15.
            static const bool fitLarge = [&] {
                unsigned v = 0;
                if (const char *e = std::getenv("DLSS5_FIT_LARGE")) return e[0] == '1' && !e[1];
                // assets may be ...\DLSS5-AMD\native-game-tiled-assets or its HIP subfolder: walk up to the flags file
                std::wstring dir = session->assetsDir;
                for (int up = 0; up < 4 && !dir.empty(); up++)
                {
                    const std::wstring flags = JoinPath(dir, L"native-game-flags.txt");
                    if (FILE *f = _wfopen(flags.c_str(), L"rb")) { char line[256]; while (fgets(line, sizeof line, f)) sscanf(line, "DLSS5_FIT_LARGE=%u", &v); fclose(f); break; }
                    const size_t cut = dir.find_last_of(L"\\/"); if (cut == std::wstring::npos) break; dir.resize(cut);
                }
                return v == 1;
            }();
            if (fitLarge)
                NativeFitLargeInputOverride() = true;
        }
        if (!NativeInputGeometry::Supported(cdesc.Width, ch, NativeFitLargeInput()))
        {
            char message[192] {};
            std::snprintf(message, sizeof message,
                          "PrepareFrame: render input %llux%u exceeds supported limit 1920x1080 (DLSS5_FIT_LARGE=0); original SR",
                          static_cast<unsigned long long>(cdesc.Width), ch);
            return Fail(LMXXF_NR_INVALID_ARGUMENT, message);
        }
        if (cw != info->color_width || ch != info->color_height)
            return Fail(LMXXF_NR_INVALID_ARGUMENT, "PrepareFrame: render input metadata/texture size mismatch; original SR");
        if (cdesc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || cdesc.DepthOrArraySize != 1 ||
            cdesc.MipLevels != 1 || cdesc.SampleDesc.Count != 1 ||
            (cdesc.Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE) ||
            !(NativeIsGameColor(cfmt) || cfmt == DXGI_FORMAT_R9G9B9E5_SHAREDEXP))
            return Fail(LMXXF_NR_INVALID_ARGUMENT, "PrepareFrame: unsupported render input texture; original SR");
        if (session->initializationBlocked)
            return Fail(LMXXF_NR_FAILED, "PrepareFrame: failed initialization could not drain safely; original SR");
        if (session->job.state != LMXXF_NR_JOB_NONE && session->job.state != LMXXF_NR_JOB_RETIRED)
            return Fail(LMXXF_NR_INVALID_ARGUMENT, "PrepareFrame: previous frame not retired; original SR");

'''
s=s.replace('        // Match upstream auto tier:', validation+'        // Match upstream auto tier:',1)
s=s.replace('    bool hipPrepared = false;', '    bool hipPrepared = false;\n    bool initializationBlocked = false;')
# Bring lazy bridge creation into the same transaction as the codec chain.
start=s.index('            if (!session->bridge)', s.index('int32_t PrepareFrame('))
end=s.index('            NativeGameCodec *enc',start)
bridge=s[start:end]
s=s[:start]+s[end:]
s=s.replace('                enc = new NativeGameCodec();', bridge+'                enc = new NativeGameCodec();',1)
s=s.replace('                delete enc;\n                throw;', r'''                delete enc;
                // No frame commands have been exposed/submitted by this transaction.
                // Prewarm can have HIP work: synchronize before releasing shared buffers.
                if (!session->bridge || session->bridge->WaitForSubmittedWork())
                {
                    session->TeardownCodecChain();
                    session->job = {};
                }
                else
                    session->initializationBlocked = true; // retain resources until safe destruction
                throw;''',1)
write(name,s)
name='dlssnr/backend/lmxxf_runtime/LmxxfProductionOptions.h';s=original(name).replace('    return o;','    o.mh_feature_byte=o.mh_proj_diag_fb=o.mh_byte_stream=o.decoder_byte=o.mh_ffn_frag256=true;\n    return o;');write(name,s)
name='dlssnr/backend/LmxxfBackend.cpp';s=original(name)
s=s.replace('    fi.model_scale = settings.modelScale;', '    fi.model_scale = settings.modelScale;\n    fi.exposure=frame.exposure;fi.exposure_state=uint32_t(frame.exposureState);\n    fi.pre_exposure=frame.preExposure;fi.exposure_scale=frame.exposureScale;')
s=s.replace('    const auto nextToDll = directory / L"lmxxf-modules";','    for(const auto& base : {directory, directory.parent_path()}){\n        const auto bundled=base / L"DLSS5-AMD" / L"native-game-tiled-assets" / L"HIP";\n        if(std::filesystem::exists(bundled / L"SHA256SUMS"))return bundled;\n    }\n    const auto nextToDll = directory / L"lmxxf-modules";')
s=s.replace('    LmxxfCut::ClearPendingEnqueue();\n    pendingJob = nullptr;','    if(pendingJob){SetStatus("lmxxf: prior job not yet submitted; original SR");return nullptr;}\n    LmxxfCut::ClearPendingEnqueue();\n    pendingJob = nullptr;',1)
s=s.replace('void LmxxfBackend::Submitted(ID3D12CommandQueue *, UINT, ID3D12CommandList *const *)\n{','void LmxxfBackend::Submitted(ID3D12CommandQueue *submittedQueue, UINT, ID3D12CommandList *const *)\n{\n    if(DlssNr::Submission::InsideLogicalExecute() || submittedQueue != queue || LmxxfCut::Pending().job)return;')
s=s.replace('        api->table.Retire(session, pendingJob);','        const auto rc=api->table.Retire(session, pendingJob);\n        if(rc != LMXXF_NR_OK){SetStatus("lmxxf: consumer retirement failed");return;}')
# Invalid input is a non-mutating NR bypass, not a request to discard a live session.
s=s.replace('const bool rebindish = frameRc == LMXXF_NR_UNAVAILABLE || (err[0] && (std::strstr(err, "rebind") || std::strstr(err, "geometry")));', 'const bool rebindish = frameRc != LMXXF_NR_INVALID_ARGUMENT && (frameRc == LMXXF_NR_UNAVAILABLE || (err[0] && (std::strstr(err, "rebind") || std::strstr(err, "geometry"))));')
write(name,s)
name='dlssnr/amd/AmdBridge.cpp';s=original(name).replace('    return std::filesystem::exists(Directory() / L"dlssnr_amd_pass1.dll", ec);','    if(DlssNr::Backend::RequestedKind()==DlssNr::Backend::Kind::Lmxxf)\n        return std::filesystem::exists(Directory() / L"LmxxfNrRuntime.dll", ec);\n    return std::filesystem::exists(Directory() / L"dlssnr_amd_pass1.dll", ec);');write(name,s)
# Aliasing barriers are forwarded verbatim into the current segment. A completed
# barrier before the cut requires no replay; splitting must not overlap an open
# transition. Aliasing does not itself change resource transition states.
name='dlssnr/submission/ResourceStateBook.h';s=original(name)
s=s.replace('                if (reasonOut)\n                    *reasonOut = "aliasing_barrier";\n                return false;','                if (openSplitBarrier || bar.Flags != D3D12_RESOURCE_BARRIER_FLAG_NONE)\n                {\n                    if (reasonOut) *reasonOut = "aliasing_during_split_transition";\n                    return false;\n                }\n                continue;',1)
s=s.replace('        if (sawAliasing)\n        {\n            if (reasonOut)\n                *reasonOut = "aliasing_barrier";\n            return false;\n        }\n','')
write(name,s)
# Current core headers/shaders are MIT; keep the host's existing GPL license separately.
for folder,patterns in [('src',('*.h',)),('Development/HIP',('*.h',)),('shaders',('*.hlsl','*.hlsli'))]:
 target=host/'third_party/lmxxf'/folder;target.mkdir(parents=True,exist_ok=True)
 for pattern in patterns:
  for p in (root/folder).glob(pattern):shutil.copyfile(p,target/p.name)
shutil.copyfile(root/'LICENSE',host/'third_party/lmxxf/LICENSE')
(here/'upstream.json').write_text(json.dumps({'repository':'https://github.com/TheAutomatic/dlss-5-amd-project','branch':'release/1.9.0','commit':rev,'core_base':subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),'host_license':'GPL-3.0 (retained upstream)','core_license':'MIT'},indent=2)+'\n')
print('prepared external host',rev)

# Harnesses are patched against the same pinned host revision.
for patch in here.glob("lmxxf_*.cpp.patch"):
    name="tests/"+patch.name.removesuffix(".patch")
    (host/name).write_text(subprocess.check_output(["git","show",rev+":"+name],cwd=host,text=True))
    subprocess.run(["git","apply",str(patch)],cwd=host,check=True)

shutil.copyfile(here/"ColourCapture.h",host/prefix/"dlssnr/backend/lmxxf_runtime/ColourCapture.h")
