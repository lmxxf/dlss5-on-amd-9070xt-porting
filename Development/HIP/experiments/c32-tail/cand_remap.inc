{static const char*ctq[]={nullptr,"ctq-ctl.hsaco","ctq-quad.hsaco"};
 if(cand>=1&&cand<=2&&module=="c32_wave1"){std::string key=std::string("ctq")+char('0'+cand);if(!modules.count(key)){Handle mm{};api.Check(api.LoadModule(&mm,(opt.modules+"/"+ctq[cand]).c_str()),ctq[cand]);modules[key]=mm;}module=key;++cand_hits;}
}
