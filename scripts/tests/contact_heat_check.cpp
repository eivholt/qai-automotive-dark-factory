#include "../../unreal/Source/DarkFactory/DarkFactoryContactHeat.h"
#include <cassert>
#include <iostream>
int main() {
    using DarkFactoryContactHeat::SurfaceC;
    assert(SurfaceC(100,12,0)==23);
    assert(SurfaceC(100,12,1)>90);
    assert(SurfaceC(100,12,10)<SurfaceC(100,12,3));
    assert(SurfaceC(100,12,400)<20.000001);
    for (double tau:{.35,5.6,17.6}) for (int k=0;k<10000;++k) {
        double age=k*.01, t=SurfaceC(100,tau,age);
        assert(std::isfinite(t) && t>=20 && t<=100);
        assert(t==SurfaceC(100,tau,age));
    }
    // Compare the closed form with an independently integrated contact ODE.
    const double dt=.0001; double numeric=23;
    for (int k=0;k<200000;++k) {
        double age=k*dt; numeric+=dt*(20+80*std::exp(-age/12)-numeric)/.35;
    }
    assert(std::abs(numeric-SurfaceC(100,12,20))<.001);
    std::cout<<"Contact response: initial state, heating, cooling, bounds, equal tau, replay, ODE agreement passed\n";
}
