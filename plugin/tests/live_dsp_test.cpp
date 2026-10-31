// Real time checks for the live pitch fix (plugin/src/LiveFx.h). Plain C++, no engine and no JUCE needed.
#define _USE_MATH_DEFINES
#include <cmath>
#include <algorithm>
#include <vector>
#include "../src/LiveFx.h"
#include <cstdio>
#include <chrono>
using namespace purr::live;
static const double SR=48000;
// frequency of a steady tone by counting rising zero crossings
static double freqOf(const std::vector<float>& x,int a,int b){int n=b-a;int lmin=SR/900,lmax=SR/80;double best=-1;int bl=0;std::vector<double> r(lmax+2);for(int l=lmin;l<=lmax;l++){double s=0,e1=0,e2=0;for(int i=a;i+l<b;i++){s+=x[i]*x[i+l];e1+=x[i]*x[i];e2+=x[i+l]*x[i+l];}r[l]=s/std::sqrt(e1*e2+1e-12);}
 for(int l=lmin;l<=lmax;l++)if(r[l]>best)best=r[l];for(int l=lmin+1;l<lmax;l++)if(r[l]>0.9*best&&r[l]>=r[l-1]&&r[l]>=r[l+1]){bl=l;break;}
 double den=r[bl-1]-2*r[bl]+r[bl+1];double dl=std::abs(den)>1e-12?0.5*(r[bl-1]-r[bl+1])/den:0;return SR/(bl+dl);}
static std::vector<float> run(const std::vector<float>&in,Settings s,int block=256){Voice v;v.prepare(SR,1);std::vector<float> out=in;for(size_t i=0;i<in.size();i+=block){int n=std::min<size_t>(block,in.size()-i);float*p=out.data()+i;float*pp[1]={p};v.process(pp,1,n,s);}return out;}
static std::vector<float> tone(double f,double sec,double amp=0.3){int n=SR*sec;std::vector<float> x(n);double ph=0;for(int i=0;i<n;i++){ph+=2*M_PI*f/SR;x[i]=amp*(std::sin(ph)+0.4*std::sin(2*ph)+0.2*std::sin(3*ph));}return x;}
static int fails=0;
#define CHECK(c,m) do{printf("%s %s\n",(c)?"ok:  ":"FAIL:",m);if(!(c))fails++;}while(0)
int main(){
 // 1. A3 + 40 cents, C major, snap 1, fast: should land on 220
 {double f=220*std::pow(2,0.4/12);auto in=tone(f,2.0);Settings s;s.snap=1;s.speed=0;s.mask=scaleMask(0,0);auto o=run(in,s);double g=freqOf(o,SR*1,SR*2);printf("1: in %.2f out %.2f\n",f,g);CHECK(std::abs(1200*std::log2(g/220.0))<12,"A3+40c snaps to A3");}
 // 2. C#4 (277.18) in C major -> C4 (261.63) or D4 (293.66)
 {auto in=tone(277.18,2.0);Settings s;s.snap=1;s.speed=0;s.mask=scaleMask(0,0);auto o=run(in,s);double g=freqOf(o,SR*1,SR*2);printf("2: out %.2f\n",g);CHECK(std::abs(g-261.63)<4||std::abs(g-293.66)<4,"C#4 goes to C4 or D4");}
 // 3. MIDI follow: sung 60.3, held 64 -> E4 329.63
 {double f=261.63*std::pow(2,0.3/12);auto in=tone(f,2.0);Settings s;s.heldNote=64;s.follow=1;s.speed=0;auto o=run(in,s);double g=freqOf(o,SR*1,SR*2);printf("3: out %.2f\n",g);CHECK(std::abs(g-329.63)<5,"held MIDI note E4 pulls the voice to E4");}
 // 4. off key: snap 0 leaves it alone
 {double f=277.18;auto in=tone(f,2.0);Settings s;s.snap=0;s.mask=scaleMask(0,0);auto o=run(in,s);double g=freqOf(o,SR*1,SR*2);CHECK(std::abs(g-f)<1.5,"snap 0 keeps the pitch");}
 // 5. latency: impulse-ish: cross-correlate a tone burst
 {auto in=std::vector<float>(SR*1,0.f);auto t=tone(220,0.2);for(size_t i=0;i<t.size();i++)in[4800+i]=t[i]*std::sin(M_PI*i/t.size());Settings s;s.snap=0;auto o=run(in,s);double bi=0,bo=0;int ci=0,co=0;for(int i=0;i<(int)in.size();i++){if(std::abs(in[i])>bi){bi=std::abs(in[i]);ci=i;}if(std::abs(o[i])>bo){bo=std::abs(o[i]);co=i;}}Voice v;v.prepare(SR,1);printf("5: peak shift %d samples, reported %d\n",co-ci,v.latencySamples());CHECK(std::abs((co-ci)-v.latencySamples())<=SR*0.004,"reported latency matches (within 4 ms)");}
 // 6. silence and noise do not blow up
 {std::vector<float> z(SR*1,0.f);Settings s;s.mask=scaleMask(0,0);auto o=run(z,s);float m=0;for(float v:o)m=std::max(m,std::abs(v));CHECK(m<1e-6,"silence in, silence out");}
 // 7. block size independence
 {auto in=tone(277.18,1.0);Settings s;s.snap=1;s.speed=0;s.mask=scaleMask(0,0);auto a=run(in,s,64),b=run(in,s,512);double d=0;for(size_t i=0;i<a.size();i++)d=std::max<double>(d,std::abs(a[i]-b[i]));printf("7: max diff %g\n",d);CHECK(d<1e-3,"same output for block 64 and 512");}
 // 8. grit on: level stays similar, no NaN
 {auto in=tone(220,1.0);Settings s;s.snap=0;s.gritOn=true;s.drive=.6;s.trans=.5;s.fuzz=.4;s.gritMix=1;s.gritPos=3;auto o=run(in,s);double ri=0,ro=0;for(int i=SR*0.5;i<SR;i++){ri+=in[i]*in[i];ro+=o[i]*o[i];if(std::isnan(o[i])){CHECK(false,"nan");}}printf("8: level ratio %.2f\n",std::sqrt(ro/ri));CHECK(std::sqrt(ro/ri)>0.6&&std::sqrt(ro/ri)<1.6,"grit keeps the level roughly");}
 // 9. auto key: sing C major scale notes -> detected C major
 {Voice v;v.prepare(SR,1);Settings s;s.autoKey=true;s.snap=0;std::vector<int> notes={60,62,64,65,67,69,71,72,67,64,60,62,64,60};std::vector<float> in;for(int n:notes){auto t=tone(midiToHz(n),0.4);in.insert(in.end(),t.begin(),t.end());}std::vector<float> out=in;for(size_t i=0;i<in.size();i+=256){int n=std::min<size_t>(256,in.size()-i);float*pp[1]={out.data()+i};v.process(pp,1,n,s);}printf("9: key %d minor %d\n",v.info.keyTonic.load(),v.info.keyMinor.load());CHECK(v.info.keyTonic.load()==0&&v.info.keyMinor.load()==0,"auto key learns C major from singing");}
 // 10. cpu
 {auto in=tone(220,10.0);Settings s;s.snap=1;s.mask=scaleMask(0,0);s.gritOn=true;s.drive=.5;s.gritPos=4;auto t0=std::chrono::steady_clock::now();auto o=run(in,s);double sec=std::chrono::duration<double>(std::chrono::steady_clock::now()-t0).count();printf("10: 10 s of audio in %.3f s (%.1f%% of one core)\n",sec,sec*10);CHECK(sec<1.5,"fast enough (under 15 percent of one core)");}

 // 11. vowel colour survives a 3 semitone shift (spectral centroid within 6 percent), formant knob moves it
 {auto vow=[&](double f0,double sec){int n=SR*sec;std::vector<float> x(n);double ph=0,y1[2]={0,0},y2[2]={0,0},F[2]={700,1500},Bw[2]={90,120};double a1[2],a2[2];for(int k=0;k<2;k++){double r=std::exp(-M_PI*Bw[k]/SR);a1[k]=2*r*std::cos(2*M_PI*F[k]/SR);a2[k]=-r*r;}
    for(int i=0;i<n;i++){ph+=f0/SR;double u=0;if(ph>=1){ph-=1;u=1;}double o=0;for(int k=0;k<2;k++){double y=u+a1[k]*y1[k]+a2[k]*y2[k];y2[k]=y1[k];y1[k]=y;o+=y;}x[i]=0.05*o;}return x;};
  auto amp=[&](const std::vector<float>&x,double f){double re=0,im=0;int a=SR*1.0,b=SR*1.8,n=b-a;for(int i=a;i<b;i++){double w=0.5-0.5*std::cos(2*M_PI*(i-a)/(n-1));re+=w*x[i]*std::cos(2*M_PI*f*i/SR);im+=w*x[i]*std::sin(2*M_PI*f*i/SR);}return std::sqrt(re*re+im*im)/n;};
  auto cent=[&](const std::vector<float>&x,double f0){double m=0,w=0;for(int k=1;k*f0<3500;k++){double a=amp(x,k*f0);m+=a*k*f0;w+=a;}return m/w;};
  auto in=vow(220,2.0);auto ref=vow(261.63,2.0);Settings s;s.heldNote=60;s.follow=1;s.speed=0;auto o=run(in,s);
  double ci=cent(ref,261.63),co=cent(o,261.63);printf("11: centroid ideal %.0f out %.0f\n",ci,co);CHECK(std::abs(co/ci-1)<0.06,"pitch shift keeps the vowel colour");
  Settings f;f.snap=0;f.formant=4;auto of=run(in,f);double c0=cent(in,220),c1=cent(of,220);printf("11b: centroid %.0f -> %.0f\n",c0,c1);CHECK(c1>c0*1.1,"formant knob moves the vowel colour up");}
 // 12. vibrato plus consonant bursts: no NaN, no loud clicks, level bounded
 {int n=SR*3;std::vector<float> in(n);double ph=0;unsigned seed=1;for(int i=0;i<n;i++){double t=i/SR;double seg=std::fmod(t,0.8);float v=0;if(seg<0.6){double f=233.08*std::pow(2,(0.35*std::sin(2*M_PI*5.5*t)+0.3)/12.0);ph+=2*M_PI*f/SR;v=0.3*(std::sin(ph)+0.4*std::sin(2*ph)+0.2*std::sin(3*ph));}else{seed=seed*1664525u+1013904223u;v=0.05f*((seed>>9)/(float)(1<<23)-0.5f)*2;}in[i]=v;}
  Settings s;s.snap=1;s.speed=0.1f;s.mask=scaleMask(0,0);auto o=run(in,s);bool nan=false;double pk=0,pki=0,maxd=0,maxdi=0;for(int i=1;i<n;i++){if(!std::isfinite(o[i]))nan=true;pk=std::max(pk,(double)std::abs(o[i]));pki=std::max(pki,(double)std::abs(in[i]));maxd=std::max(maxd,(double)std::abs(o[i]-o[i-1]));maxdi=std::max(maxdi,(double)std::abs(in[i]-in[i-1]));}
  printf("12: peak out %.3f in %.3f, max step out %.3f in %.3f\n",pk,pki,maxd,maxdi);CHECK(!nan&&pk<1.4*pki,"phrase with vibrato and bursts: no NaN, bounded level");CHECK(maxd<2.0*maxdi+0.02,"no clicks (largest sample step stays near the input's)");}
 // 13. note hysteresis: singing exactly between two notes (with a little wobble) does not flip the target
 {Voice v;v.prepare(SR,1);Settings s;s.snap=1;s.speed=0;s.mask=scaleMask(0,0);unsigned seed=7;double ph=0;std::vector<float> in(SR*2);std::vector<float> out;for(size_t i=0;i<in.size();i+=256){seed=seed*1664525u+1013904223u;double wob=((seed>>9)/(double)(1<<23)-0.5)*0.4;double f=midiToHz(61.0+wob);for(int k=0;k<256;k++){ph+=2*M_PI*f/SR;in[i+k]=0.3*(std::sin(ph)+0.4*std::sin(2*ph));}}
  std::vector<float> o=in;int flips=0,last=-1;for(size_t i=0;i<o.size();i+=256){float*pp[1]={o.data()+i};v.process(pp,1,256,s);float t=v.info.target.load();if(i>SR*0.5&&t>0){int r=(int)std::lround(t);if(last>=0&&r!=last)flips++;last=r;}}
  printf("13: target flips %d, last %d\n",flips,last);CHECK(flips<=1,"target note does not flicker between two notes");}
 return fails;}
