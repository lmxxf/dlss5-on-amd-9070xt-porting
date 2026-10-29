from pathlib import Path
p=Path(__file__).parent;w=p/'work/frame';w.mkdir(parents=True,exist_ok=True)
s=Path('src/native_game_frame.h').read_text();key='auto DiagnosticNetworkTiming(){return resources->network.PollNetworkTiming();}';assert key in s;s=s.replace(key,key+'\n std::vector<double> DiagnosticStageTiming(){std::vector<double> v;auto&r=*resources;if(!r.probe_on||!r.probe.Intervals(r.submit.TimestampFrequency(),v))throw std::runtime_error("stage timestamps unavailable");return v;}');(w/'native_game_frame.h').write_text(s)
s=Path('Development/HIP/benchmark_live_capture.cpp').read_text();a=s.index('#ifdef DLSS5_BENCH_BRIDGE_ISOLATE\n');b=s.index('\n#endif',a);s=s[:a]+s[b+len('\n#endif'):]
s=s.replace('flags(argv[2]);','flags(argv[2]);env("DLSS5_NET_TIMING","1");env("DLSS5_GAME_PROBE","1");')
s=s.replace('for(UINT i=0;i<N;i++){','FILE*stage=_wfopen((prefix+L"-stages.csv").c_str(),L"wb");if(!stage)throw std::runtime_error("stages open");fprintf(stage,"frame,frame_wall_ms,nn_gpu_ms,encode_ms,input_ms,bridge_network_ms,neural_ms,decode_ms,copy_ms\\n");\n for(UINT i=0;i<N;i++){')
key='walltotal+=elapsed;';assert key in s;s=s.replace(key,key+'\n auto nt=frame.DiagnosticNetworkTiming();auto st=frame.DiagnosticStageTiming();if(!nt.valid||st.size()!=6)throw std::runtime_error("NN/stage timestamp invalid");fprintf(stage,"%u,%.9f,%.9f",i,elapsed,double(nt.ms));for(double v:st)fprintf(stage,",%.9f",v);fprintf(stage,"\\n");fflush(stage);')
s=s.replace('fclose(csv);save_frame','fclose(stage);fclose(csv);save_frame');assert 'FILE*stage='in s;(w/'frame.cpp').write_text(s)
