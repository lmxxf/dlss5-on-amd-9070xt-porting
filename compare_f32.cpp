#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <vector>

int wmain(int argc, wchar_t **argv) {
    if (argc != 3) return 2;
    std::ifstream a(argv[1], std::ios::binary), b(argv[2], std::ios::binary);
    if (!a || !b) return 2;
    constexpr size_t count = 1 << 20;
    std::vector<float> x(count), y(count);
    long double sx=0,sy=0,sxx=0,syy=0,sxy=0,ae=0,se=0;
    uint64_t n=0, exact=0;
    for (;;) {
        a.read(reinterpret_cast<char *>(x.data()), x.size()*4);
        b.read(reinterpret_cast<char *>(y.data()), y.size()*4);
        const size_t na=static_cast<size_t>(a.gcount())/4, nb=static_cast<size_t>(b.gcount())/4;
        if (na != nb) return 3;
        for (size_t i=0;i<na;++i) { const long double p=x[i],q=y[i],d=p-q;sx+=p;sy+=q;sxx+=p*p;syy+=q*q;sxy+=p*q;ae+=std::abs(d);se+=d*d;exact+=p==q; }
        n += na;
        if (na != count) break;
    }
    if (!n) return 3;
    const long double dn=n;
    const long double corr=(sxy-sx*sy/dn)/std::sqrt((sxx-sx*sx/dn)*(syy-sy*sy/dn));
    const long double std_x=std::sqrt(sxx/dn-(sx/dn)*(sx/dn));
    const long double std_y=std::sqrt(syy/dn-(sy/dn)*(sy/dn));
    const long double rmse=std::sqrt(se/dn);
    std::printf("count=%llu corr=%.9Lf mae=%.9Lf rmse=%.9Lf nrmse=%.9Lf std_a=%.9Lf std_b=%.9Lf exact=%.9Lf\n",
        static_cast<unsigned long long>(n),corr,ae/dn,rmse,rmse/std_x,std_x,std_y,(long double)exact/dn);
    return 0;
}
