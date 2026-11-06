#pragma once
#include <algorithm>
#include <cmath>
namespace pocket {
// A bounded, allocation-free colour stage shared by sampler and synth voices.
class SampleEffects {
public:
    static float cutoffHz(unsigned value){return 30.f*std::pow(10000.f/30.f,std::min(value,99u)/99.f);}
    void configure(unsigned cutoff,unsigned resonance,unsigned drive,unsigned bits,bool modulated=false) {
        cutoff=std::min(cutoff,100u);resonance=std::min(resonance,90u);drive=std::min(drive,100u);bits=std::max(4u,std::min(bits,16u));
        if(cutoff!=lastCut||resonance!=lastRes||modulated!=lastModulated){
            if(modulated){static const float coefficients[101]={0.003927011003f,0.004164338459f,0.004416009064f,0.00468288973f,0.004965899778f,0.005266014111f,0.005584266577f,0.005921753535f,0.006279637637f,0.00665915185f,0.007061603703f,0.007488379814f,0.007940950675f,0.008420875738f,0.008929808812f,0.009469503782f,0.01004182068f,0.01064873215f,0.01129233026f,0.01197483377f,0.01269859586f,0.01346611231f,0.01428003016f,0.01514315697f,0.01605847061f,0.0170291297f,0.01805848464f,0.01915008941f,0.02030771406f,0.02153535806f,0.0228372644f,0.02421793472f,0.02568214534f,0.02723496436f,0.02888176993f,0.03062826967f,0.03248052149f,0.03444495573f,0.03652839896f,0.03873809933f,0.04108175383f,0.04356753751f,0.04620413481f,0.04900077338f,0.05196726043f,0.05511402199f,0.05845214539f,0.06199342527f,0.06575041355f,0.06973647387f,0.07396584096f,0.07845368566f,0.08321618632f,0.0882706074f,0.09363538629f,0.09933022974f,0.105376221f,0.1117959395f,0.1186135958f,0.125855182f,0.1335486442f,0.1417240762f,0.1504139421f,0.1596533313f,0.1694802513f,0.1799359686f,0.191065403f,0.2029175902f,0.2155462248f,0.2290103014f,0.2433748761f,0.2587119751f,0.2751016861f,0.2926334757f,0.3114077904f,0.3315380141f,0.3531528787f,0.3763994533f,0.4014468812f,0.4284910897f,0.4577607814f,0.4895251291f,0.5241037678f,0.5618799213f,0.6033178724f,0.6489865487f,0.6995918757f,0.7560219494f,0.8194113795f,0.8912350235f,0.9734480831f,1.068701754f,1.180686715f,1.314702658f,1.47864896f,1.68485117f,1.953681145f,2.321429023f,2.859673163f,3.732050808f,3.732050808f};targetG=coefficients[cutoff];}
            else targetG=std::tan(3.14159265358979323846f*cutoffHz(cutoff)/24000.f);
            lastModulated=modulated;
            targetK=1.f/(.70710678f+resonance*.04f);lastCut=cutoff;lastRes=resonance;
        }
        targetWet=cutoff<100?1.f:0.f;targetDrive=float(drive);
        if(bits!=lastBits){targetStep=std::pow(2.f,float(16-bits));lastBits=bits;}
        if(!configured){g=targetG;k=targetK;wet=targetWet;drive=targetDrive;step=targetStep;configured=true;}
    }
    void resetState(){ic1=ic2=0;}
    __attribute__((always_inline)) float process(float input) {
        if(lastModulated&&targetDrive==0&&targetStep==1){
            wet+=(targetWet-wet)*.002f;g+=(targetG-g)*.002f;k+=(targetK-k)*.002f;
            // Coherent TPT coefficients at 1500Hz avoid a software division per sample.
            if(!filterClock){fastA1=1.f/(1.f+g*(g+k));fastA2=g*fastA1;fastA3=g*fastA2;}
            filterClock=(filterClock+1)&15u;
            if(targetWet||wet>.0001f){float v3=input-ic2,v1=fastA1*ic1+fastA2*v3,v2=ic2+fastA2*ic1+fastA3*v3;ic1=2*v1-ic1;ic2=2*v2-ic2;return input+(v2-input)*wet;}
            resetState();return input;
        }
        wet+=(targetWet-wet)*.002f;g+=(targetG-g)*.002f;k+=(targetK-k)*.002f;
        drive+=(targetDrive-drive)*.002f;step+=(targetStep-step)*.002f;if(std::fabs(targetStep-step)<.125f)step=targetStep;
        float value=input;
        if(targetWet||wet>.0001f){
            float a1=1.f/(1.f+g*(g+k)),a2=g*a1,a3=g*a2;
            float v3=input-ic2,v1=a1*ic1+a2*v3,v2=ic2+a2*ic1+a3*v3;
            ic1=2*v1-ic1;ic2=2*v2-ic2;
            value=input+(v2-input)*wet;
        }else resetState();
        if(drive>.001f){float x=value/32768.f;float pushed=x*(1.f+drive*.15f);float shaped=pushed/(1.f+std::fabs(pushed));float mix=std::min(1.f,drive/20.f);value+=(shaped*32768.f-value)*mix;}
        if(targetStep>1.f||step>1.001f)value=std::round(value/step)*step;
        return value;
    }
private:
    bool configured=false,lastModulated=false;unsigned filterClock=0;float fastA1=1,fastA2=0,fastA3=0;
    unsigned lastCut=100,lastRes=0,lastBits=16;
    float targetG=3.7320508f,targetK=1.41421356f,targetWet=0,targetDrive=0,targetStep=1;
    float g=3.7320508f,k=1.41421356f,wet=0,drive=0,step=1,ic1=0,ic2=0;
};
}
