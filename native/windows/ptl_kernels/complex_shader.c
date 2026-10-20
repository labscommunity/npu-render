/* Generated structurally lowered shader experiment. No host rasterization. */
#pragma STDC FP_CONTRACT OFF
#ifndef INLINE
#define INLINE static __attribute__((always_inline)) inline
#endif
INLINE float nr_float(unsigned v){union{unsigned u;float f;}x;x.u=v;return x.f;}
INLINE unsigned nr_bits(float v){union{unsigned u;float f;}x;x.f=v;return x.u;}
INLINE float nr_flush(float v){return (nr_bits(v)&0x7f800000u)?v:0.0f;}
INLINE float nr_min(float a,float b){if(a!=a)return b;if(b!=b)return a;return a<b?a:b;}
INLINE float nr_max(float a,float b){if(a!=a)return b;if(b!=b)return a;return a<b?b:a;}
INLINE float nr_saturate(float a){a=nr_flush(a);return !(a>=0)?0:(a>1?1:a);}
static __attribute__((noinline)) float nr_floor(float a){
 unsigned bits=nr_bits(a),mag=bits&0x7fffffffu;
 if(mag<0x00800000u)return nr_float(bits&0x80000000u);
 if(mag>=0x4b000000u)return a;
 int i=(int)a;float b=(float)i;return b>a?b-1.0f:b;
}
INLINE float nr_fraction(float a){return (nr_bits(a)&0x7fffffffu)<0x00800000u?0.0f:a-nr_floor(a);}
INLINE void nr_coordinates(const unsigned *input,const unsigned *constant,const unsigned *sampled,unsigned *coordinates){
 unsigned s0=constant[8];
 unsigned s1=constant[9];
 unsigned s2=constant[10];
 unsigned s3=constant[11];
 unsigned s4=nr_bits((nr_float(input[8])/nr_float(input[10])));
 unsigned s5=nr_bits((nr_float(input[9])/nr_float(input[10])));
 unsigned s6=nr_bits(nr_max(nr_float(s4),nr_float(s0)));
 unsigned s7=nr_bits(nr_max(nr_float(s5),nr_float(s1)));
 unsigned s8=nr_bits(nr_min(nr_float(s6),nr_float(s2)));
 unsigned s9=nr_bits(nr_min(nr_float(s7),nr_float(s3)));
 unsigned s14=constant[16];
 unsigned s15=constant[17];
 unsigned s16=constant[18];
 unsigned s17=constant[19];
 unsigned s18=constant[20];
 unsigned s19=constant[21];
 unsigned s20=constant[22];
 unsigned s21=constant[23];
 unsigned s22=nr_bits((nr_float(input[12])/nr_float(input[14])));
 unsigned s23=nr_bits((nr_float(input[13])/nr_float(input[14])));
 unsigned s24=s22;
 unsigned s25=s23;
 unsigned s26=nr_bits((nr_float(s24)*nr_float(s18)));
 unsigned s27=nr_bits((nr_float(s25)*nr_float(s19)));
 unsigned s28=nr_bits(nr_floor(nr_float(s26)));
 unsigned s29=nr_bits(nr_floor(nr_float(s27)));
 unsigned s30=nr_bits((nr_float(s28)+nr_float(1056964608u)));
 unsigned s31=nr_bits((nr_float(s29)+nr_float(1056964608u)));
 unsigned s32=nr_bits(((nr_float(s24)*nr_float(s18))+nr_float((s30^0x80000000u))));
 unsigned s33=nr_bits(((nr_float(s25)*nr_float(s19))+nr_float((s31^0x80000000u))));
 unsigned s34=(nr_float(0u)<nr_float(s32)?0xffffffffu:0u);
 unsigned s35=(nr_float(0u)<nr_float(s33)?0xffffffffu:0u);
 unsigned s36=(nr_float(s32)<nr_float(0u)?0xffffffffu:0u);
 unsigned s37=(nr_float(s33)<nr_float(0u)?0xffffffffu:0u);
 unsigned s38=((0u-s34)+s36);
 unsigned s39=((0u-s35)+s37);
 unsigned s40=nr_bits((float)(int)s38);
 unsigned s41=nr_bits((float)(int)s39);
 unsigned s42=nr_bits((nr_float(s30)+nr_float(s40)));
 unsigned s43=nr_bits((nr_float(s31)+nr_float(s41)));
 unsigned s44=nr_bits(((nr_float(s30)*nr_float(s20))+nr_float((s14^0x80000000u))));
 unsigned s45=nr_bits(((nr_float(s31)*nr_float(s21))+nr_float((s15^0x80000000u))));
 unsigned s46=nr_bits(((nr_float(s42)*nr_float(s20))+nr_float((s14^0x80000000u))));
 unsigned s47=nr_bits(((nr_float(s43)*nr_float(s21))+nr_float((s15^0x80000000u))));
 unsigned s48=nr_bits((nr_float((s14^0x80000000u))+nr_float(s16)));
 unsigned s49=nr_bits((nr_float((s15^0x80000000u))+nr_float(s17)));
 unsigned s50=nr_bits((nr_float((s14^0x80000000u))+nr_float(s16)));
 unsigned s51=nr_bits((nr_float((s15^0x80000000u))+nr_float(s17)));
 unsigned s52=nr_bits((nr_float(s44)/nr_float(s48)));
 unsigned s53=nr_bits((nr_float(s45)/nr_float(s49)));
 unsigned s54=nr_bits((nr_float(s46)/nr_float(s50)));
 unsigned s55=nr_bits((nr_float(s47)/nr_float(s51)));
 unsigned s56=(nr_flush(nr_float(s52))>=nr_flush(nr_float((s52^0x80000000u)))?0xffffffffu:0u);
 unsigned s57=(nr_flush(nr_float(s53))>=nr_flush(nr_float((s53^0x80000000u)))?0xffffffffu:0u);
 unsigned s58=(nr_flush(nr_float(s54))>=nr_flush(nr_float((s54^0x80000000u)))?0xffffffffu:0u);
 unsigned s59=(nr_flush(nr_float(s55))>=nr_flush(nr_float((s55^0x80000000u)))?0xffffffffu:0u);
 unsigned s60=nr_bits(nr_fraction(nr_float((s52&0x7fffffffu))));
 unsigned s61=nr_bits(nr_fraction(nr_float((s53&0x7fffffffu))));
 unsigned s62=nr_bits(nr_fraction(nr_float((s54&0x7fffffffu))));
 unsigned s63=nr_bits(nr_fraction(nr_float((s55&0x7fffffffu))));
 unsigned s64=(s56?s60:(s60^0x80000000u));
 unsigned s65=(s57?s61:(s61^0x80000000u));
 unsigned s66=(s58?s62:(s62^0x80000000u));
 unsigned s67=(s59?s63:(s63^0x80000000u));
 unsigned s68=nr_bits(((nr_float(s64)*nr_float(s48))+nr_float(s48)));
 unsigned s69=nr_bits(((nr_float(s65)*nr_float(s49))+nr_float(s49)));
 unsigned s70=nr_bits(((nr_float(s66)*nr_float(s50))+nr_float(s50)));
 unsigned s71=nr_bits(((nr_float(s67)*nr_float(s51))+nr_float(s51)));
 unsigned s72=nr_bits(((nr_float(s64)*nr_float(s48))+nr_float((s68^0x80000000u))));
 unsigned s73=nr_bits(((nr_float(s65)*nr_float(s49))+nr_float((s69^0x80000000u))));
 unsigned s74=nr_bits(((nr_float(s66)*nr_float(s50))+nr_float((s70^0x80000000u))));
 unsigned s75=nr_bits(((nr_float(s67)*nr_float(s51))+nr_float((s71^0x80000000u))));
 unsigned s76=nr_bits((nr_float(s48)*nr_float(s64)));
 unsigned s77=nr_bits((nr_float(s49)*nr_float(s65)));
 unsigned s78=nr_bits((nr_float(s50)*nr_float(s66)));
 unsigned s79=nr_bits((nr_float(s51)*nr_float(s67)));
 unsigned s80=(nr_flush(nr_float(s76))>=nr_flush(nr_float(0u))?0xffffffffu:0u);
 unsigned s81=(nr_flush(nr_float(s77))>=nr_flush(nr_float(0u))?0xffffffffu:0u);
 unsigned s82=(nr_flush(nr_float(s78))>=nr_flush(nr_float(0u))?0xffffffffu:0u);
 unsigned s83=(nr_flush(nr_float(s79))>=nr_flush(nr_float(0u))?0xffffffffu:0u);
 unsigned s84=(s80&1065353216u);
 unsigned s85=(s81&1065353216u);
 unsigned s86=(s82&1065353216u);
 unsigned s87=(s83&1065353216u);
 unsigned s88=nr_bits(((nr_float(s84)*nr_float(s72))+nr_float(s68)));
 unsigned s89=nr_bits(((nr_float(s85)*nr_float(s73))+nr_float(s69)));
 unsigned s90=nr_bits(((nr_float(s86)*nr_float(s74))+nr_float(s70)));
 unsigned s91=nr_bits(((nr_float(s87)*nr_float(s75))+nr_float(s71)));
 unsigned s92=nr_bits((nr_float(s88)+nr_float(s14)));
 unsigned s93=nr_bits((nr_float(s89)+nr_float(s15)));
 unsigned s94=nr_bits((nr_float(s90)+nr_float(s14)));
 unsigned s95=nr_bits((nr_float(s91)+nr_float(s15)));
 coordinates[0]=s8;
 coordinates[1]=s9;
 coordinates[2]=s94;
 coordinates[3]=s95;
 coordinates[4]=s92;
 coordinates[5]=s95;
 coordinates[6]=s94;
 coordinates[7]=s93;
 coordinates[8]=s92;
 coordinates[9]=s93;
}
INLINE void nr_color(const unsigned *input,const unsigned *constant,const unsigned *sampled,unsigned *color){
 unsigned s10=sampled[0];
 unsigned s11=sampled[1];
 unsigned s12=sampled[2];
 unsigned s13=sampled[3];
 unsigned s18=constant[20];
 unsigned s19=constant[21];
 unsigned s22=nr_bits((nr_float(input[12])/nr_float(input[14])));
 unsigned s23=nr_bits((nr_float(input[13])/nr_float(input[14])));
 unsigned s24=s22;
 unsigned s25=s23;
 unsigned s26=nr_bits((nr_float(s24)*nr_float(s18)));
 unsigned s27=nr_bits((nr_float(s25)*nr_float(s19)));
 unsigned s28=nr_bits(nr_floor(nr_float(s26)));
 unsigned s29=nr_bits(nr_floor(nr_float(s27)));
 unsigned s30=nr_bits((nr_float(s28)+nr_float(1056964608u)));
 unsigned s31=nr_bits((nr_float(s29)+nr_float(1056964608u)));
 unsigned s32=nr_bits(((nr_float(s24)*nr_float(s18))+nr_float((s30^0x80000000u))));
 unsigned s33=nr_bits(((nr_float(s25)*nr_float(s19))+nr_float((s31^0x80000000u))));
 unsigned s96=sampled[4];
 unsigned s97=sampled[5];
 unsigned s98=sampled[6];
 unsigned s99=sampled[7];
 unsigned s100=sampled[8];
 unsigned s101=sampled[9];
 unsigned s102=sampled[10];
 unsigned s103=sampled[11];
 unsigned s104=nr_bits((nr_float(s96)+nr_float((s100^0x80000000u))));
 unsigned s105=nr_bits((nr_float(s97)+nr_float((s101^0x80000000u))));
 unsigned s106=nr_bits((nr_float(s98)+nr_float((s102^0x80000000u))));
 unsigned s107=nr_bits((nr_float(s99)+nr_float((s103^0x80000000u))));
 unsigned s108=nr_bits(((nr_float((s32&0x7fffffffu))*nr_float(s104))+nr_float(s100)));
 unsigned s109=nr_bits(((nr_float((s32&0x7fffffffu))*nr_float(s105))+nr_float(s101)));
 unsigned s110=nr_bits(((nr_float((s32&0x7fffffffu))*nr_float(s106))+nr_float(s102)));
 unsigned s111=nr_bits(((nr_float((s32&0x7fffffffu))*nr_float(s107))+nr_float(s103)));
 unsigned s112=sampled[12];
 unsigned s113=sampled[13];
 unsigned s114=sampled[14];
 unsigned s115=sampled[15];
 unsigned s116=sampled[16];
 unsigned s117=sampled[17];
 unsigned s118=sampled[18];
 unsigned s119=sampled[19];
 unsigned s120=nr_bits((nr_float((s116^0x80000000u))+nr_float(s112)));
 unsigned s121=nr_bits((nr_float((s117^0x80000000u))+nr_float(s113)));
 unsigned s122=nr_bits((nr_float((s118^0x80000000u))+nr_float(s114)));
 unsigned s123=nr_bits((nr_float((s119^0x80000000u))+nr_float(s115)));
 unsigned s124=nr_bits(((nr_float((s32&0x7fffffffu))*nr_float(s120))+nr_float(s116)));
 unsigned s125=nr_bits(((nr_float((s32&0x7fffffffu))*nr_float(s121))+nr_float(s117)));
 unsigned s126=nr_bits(((nr_float((s32&0x7fffffffu))*nr_float(s122))+nr_float(s118)));
 unsigned s127=nr_bits(((nr_float((s32&0x7fffffffu))*nr_float(s123))+nr_float(s119)));
 unsigned s128=nr_bits((nr_float((s124^0x80000000u))+nr_float(s108)));
 unsigned s129=nr_bits((nr_float((s125^0x80000000u))+nr_float(s109)));
 unsigned s130=nr_bits((nr_float((s126^0x80000000u))+nr_float(s110)));
 unsigned s131=nr_bits((nr_float((s127^0x80000000u))+nr_float(s111)));
 unsigned s132=nr_bits(((nr_float((s33&0x7fffffffu))*nr_float(s128))+nr_float(s124)));
 unsigned s133=nr_bits(((nr_float((s33&0x7fffffffu))*nr_float(s129))+nr_float(s125)));
 unsigned s134=nr_bits(((nr_float((s33&0x7fffffffu))*nr_float(s130))+nr_float(s126)));
 unsigned s135=nr_bits(((nr_float((s33&0x7fffffffu))*nr_float(s131))+nr_float(s127)));
 unsigned s136=(nr_flush(nr_float(s13))==nr_flush(nr_float(0u))?0xffffffffu:0u);
 unsigned s137=nr_bits((nr_float(s10)/nr_float(s13)));
 unsigned s138=nr_bits((nr_float(s11)/nr_float(s13)));
 unsigned s139=nr_bits((nr_float(s12)/nr_float(s13)));
 unsigned s140=(s136?0u:s137);
 unsigned s141=(s136?0u:s138);
 unsigned s142=(s136?0u:s139);
 unsigned s143=nr_bits((((nr_float(1050253722u)*nr_float(s140))+(nr_float(1058474557u)*nr_float(s141)))+(nr_float(1038174126u)*nr_float(s142))));
 unsigned s144=(nr_flush(nr_float(0u))==nr_flush(nr_float(constant[3]))?0xffffffffu:0u);
 unsigned s145=nr_bits((nr_float(constant[0])/nr_float(constant[3])));
 unsigned s146=nr_bits((nr_float(constant[1])/nr_float(constant[3])));
 unsigned s147=nr_bits((nr_float(constant[2])/nr_float(constant[3])));
 unsigned s148=(s144?0u:s145);
 unsigned s149=(s144?0u:s146);
 unsigned s150=(s144?0u:s147);
 unsigned s151=nr_bits((((nr_float(1050253722u)*nr_float(s148))+(nr_float(1058474557u)*nr_float(s149)))+(nr_float(1038174126u)*nr_float(s150))));
 unsigned s152=nr_bits((nr_float((s143^0x80000000u))+nr_float(s151)));
 unsigned s153=nr_bits((nr_float(s152)+nr_float(s140)));
 unsigned s154=nr_bits((nr_float(s152)+nr_float(s141)));
 unsigned s155=nr_bits((nr_float(s152)+nr_float(s142)));
 unsigned s156=nr_bits((nr_float((s153^0x80000000u))+nr_float(1065353216u)));
 unsigned s157=nr_bits((nr_float((s154^0x80000000u))+nr_float(1065353216u)));
 unsigned s158=nr_bits((nr_float((s155^0x80000000u))+nr_float(1065353216u)));
 unsigned s159=nr_bits((nr_float((s153^0x80000000u))+nr_float(s151)));
 unsigned s160=nr_bits((nr_float((s154^0x80000000u))+nr_float(s151)));
 unsigned s161=nr_bits((nr_float((s155^0x80000000u))+nr_float(s151)));
 unsigned s162=nr_bits((nr_float(s156)/nr_float(s159)));
 unsigned s163=nr_bits((nr_float(s157)/nr_float(s160)));
 unsigned s164=nr_bits((nr_float(s158)/nr_float(s161)));
 unsigned s165=(nr_flush(nr_float(s153))>=nr_flush(nr_float(1065353216u))?0xffffffffu:0u);
 unsigned s166=(nr_flush(nr_float(s154))>=nr_flush(nr_float(1065353216u))?0xffffffffu:0u);
 unsigned s167=(nr_flush(nr_float(s155))>=nr_flush(nr_float(1065353216u))?0xffffffffu:0u);
 unsigned s168=(s165&1065353216u);
 unsigned s169=(s166&1065353216u);
 unsigned s170=(s167&1065353216u);
 unsigned s171=nr_bits((nr_float(s162)*nr_float(s168)));
 unsigned s172=nr_bits((nr_float(s163)*nr_float(s169)));
 unsigned s173=nr_bits((nr_float(s164)*nr_float(s170)));
 unsigned s174=nr_bits((nr_float((s153^0x80000000u))/nr_float(s159)));
 unsigned s175=nr_bits((nr_float((s154^0x80000000u))/nr_float(s160)));
 unsigned s176=nr_bits((nr_float((s155^0x80000000u))/nr_float(s161)));
 unsigned s177=(nr_flush(nr_float(s153))>=nr_flush(nr_float(0u))?0xffffffffu:0u);
 unsigned s178=(nr_flush(nr_float(s154))>=nr_flush(nr_float(0u))?0xffffffffu:0u);
 unsigned s179=(nr_flush(nr_float(s155))>=nr_flush(nr_float(0u))?0xffffffffu:0u);
 unsigned s180=(s177?0u:1065353216u);
 unsigned s181=(s178?0u:1065353216u);
 unsigned s182=(s179?0u:1065353216u);
 unsigned s183=nr_bits((nr_float(s174)*nr_float(s180)));
 unsigned s184=nr_bits((nr_float(s175)*nr_float(s181)));
 unsigned s185=nr_bits((nr_float(s176)*nr_float(s182)));
 unsigned s186=nr_bits(nr_max(nr_float(s171),nr_float(s183)));
 unsigned s187=nr_bits(nr_max(nr_float(s172),nr_float(s184)));
 unsigned s188=nr_bits(nr_max(nr_float(s173),nr_float(s185)));
 unsigned s189=nr_bits(nr_max(nr_float(s187),nr_float(s186)));
 unsigned s190=nr_bits(nr_max(nr_float(s188),nr_float(s189)));
 unsigned s191=nr_bits(nr_max(nr_float(s190),nr_float(0u)));
 unsigned s192=nr_bits(((nr_float(s191)*nr_float(s159))+nr_float(s153)));
 unsigned s193=nr_bits(((nr_float(s191)*nr_float(s160))+nr_float(s154)));
 unsigned s194=nr_bits(((nr_float(s191)*nr_float(s161))+nr_float(s155)));
 unsigned s195=nr_bits((nr_float(s192)*nr_float(constant[3])));
 unsigned s196=nr_bits((nr_float(s193)*nr_float(constant[3])));
 unsigned s197=nr_bits((nr_float(s194)*nr_float(constant[3])));
 unsigned s198=nr_bits((nr_float((s13^0x80000000u))+nr_float(1065353216u)));
 unsigned s199=nr_bits((nr_float(s198)*nr_float(constant[0])));
 unsigned s200=nr_bits((nr_float(s198)*nr_float(constant[1])));
 unsigned s201=nr_bits((nr_float(s198)*nr_float(constant[2])));
 unsigned s202=nr_bits(((nr_float(s195)*nr_float(s13))+nr_float(s199)));
 unsigned s203=nr_bits(((nr_float(s196)*nr_float(s13))+nr_float(s200)));
 unsigned s204=nr_bits(((nr_float(s197)*nr_float(s13))+nr_float(s201)));
 unsigned s205=nr_bits((nr_float(1065353216u)+nr_float((constant[3]^0x80000000u))));
 unsigned s206=nr_bits(nr_saturate(nr_float(nr_bits(((nr_float(s10)*nr_float(s205))+nr_float(s202))))));
 unsigned s207=nr_bits(nr_saturate(nr_float(nr_bits(((nr_float(s11)*nr_float(s205))+nr_float(s203))))));
 unsigned s208=nr_bits(nr_saturate(nr_float(nr_bits(((nr_float(s12)*nr_float(s205))+nr_float(s204))))));
 unsigned s209=nr_bits((nr_float(1065353216u)+nr_float((constant[7]^0x80000000u))));
 unsigned s210=nr_bits((nr_float(s209)*nr_float(s206)));
 unsigned s211=nr_bits((nr_float(s209)*nr_float(s207)));
 unsigned s212=nr_bits((nr_float(s209)*nr_float(s208)));
 unsigned s213=nr_bits((nr_float(s13)+nr_float(constant[3])));
 unsigned s214=nr_bits(((nr_float((s13^0x80000000u))*nr_float(constant[3]))+nr_float(s213)));
 unsigned s215=nr_bits((nr_float(s206)/nr_float(s214)));
 unsigned s216=nr_bits((nr_float(s207)/nr_float(s214)));
 unsigned s217=nr_bits((nr_float(s208)/nr_float(s214)));
 unsigned s218=(nr_flush(nr_float(s214))==nr_flush(nr_float(0u))?0xffffffffu:0u);
 unsigned s219=(s218?0u:s215);
 unsigned s220=(s218?0u:s216);
 unsigned s221=(s218?0u:s217);
 unsigned s222=nr_bits((((nr_float(1050253722u)*nr_float(s219))+(nr_float(1058474557u)*nr_float(s220)))+(nr_float(1038174126u)*nr_float(s221))));
 unsigned s223=(nr_flush(nr_float(0u))==nr_flush(nr_float(constant[7]))?0xffffffffu:0u);
 unsigned s224=nr_bits((nr_float(constant[4])/nr_float(constant[7])));
 unsigned s225=nr_bits((nr_float(constant[5])/nr_float(constant[7])));
 unsigned s226=nr_bits((nr_float(constant[6])/nr_float(constant[7])));
 unsigned s227=(s223?0u:s224);
 unsigned s228=(s223?0u:s225);
 unsigned s229=(s223?0u:s226);
 unsigned s230=nr_bits((((nr_float(1050253722u)*nr_float(s227))+(nr_float(1058474557u)*nr_float(s228)))+(nr_float(1038174126u)*nr_float(s229))));
 unsigned s231=nr_bits((nr_float((s230^0x80000000u))+nr_float(s222)));
 unsigned s232=nr_bits((nr_float(s231)+nr_float(s227)));
 unsigned s233=nr_bits((nr_float(s231)+nr_float(s228)));
 unsigned s234=nr_bits((nr_float(s231)+nr_float(s229)));
 unsigned s235=nr_bits((nr_float((s232^0x80000000u))+nr_float(1065353216u)));
 unsigned s236=nr_bits((nr_float((s233^0x80000000u))+nr_float(1065353216u)));
 unsigned s237=nr_bits((nr_float((s234^0x80000000u))+nr_float(1065353216u)));
 unsigned s238=nr_bits((nr_float(s222)+nr_float((s232^0x80000000u))));
 unsigned s239=nr_bits((nr_float(s222)+nr_float((s233^0x80000000u))));
 unsigned s240=nr_bits((nr_float(s222)+nr_float((s234^0x80000000u))));
 unsigned s241=nr_bits((nr_float(s235)/nr_float(s238)));
 unsigned s242=nr_bits((nr_float(s236)/nr_float(s239)));
 unsigned s243=nr_bits((nr_float(s237)/nr_float(s240)));
 unsigned s244=(nr_flush(nr_float(s232))>=nr_flush(nr_float(1065353216u))?0xffffffffu:0u);
 unsigned s245=(nr_flush(nr_float(s233))>=nr_flush(nr_float(1065353216u))?0xffffffffu:0u);
 unsigned s246=(nr_flush(nr_float(s234))>=nr_flush(nr_float(1065353216u))?0xffffffffu:0u);
 unsigned s247=(s244&1065353216u);
 unsigned s248=(s245&1065353216u);
 unsigned s249=(s246&1065353216u);
 unsigned s250=nr_bits((nr_float(s241)*nr_float(s247)));
 unsigned s251=nr_bits((nr_float(s242)*nr_float(s248)));
 unsigned s252=nr_bits((nr_float(s243)*nr_float(s249)));
 unsigned s253=nr_bits((nr_float((s232^0x80000000u))/nr_float(s238)));
 unsigned s254=nr_bits((nr_float((s233^0x80000000u))/nr_float(s239)));
 unsigned s255=nr_bits((nr_float((s234^0x80000000u))/nr_float(s240)));
 unsigned s256=(nr_flush(nr_float(s232))>=nr_flush(nr_float(0u))?0xffffffffu:0u);
 unsigned s257=(nr_flush(nr_float(s233))>=nr_flush(nr_float(0u))?0xffffffffu:0u);
 unsigned s258=(nr_flush(nr_float(s234))>=nr_flush(nr_float(0u))?0xffffffffu:0u);
 unsigned s259=(s256?0u:1065353216u);
 unsigned s260=(s257?0u:1065353216u);
 unsigned s261=(s258?0u:1065353216u);
 unsigned s262=nr_bits((nr_float(s253)*nr_float(s259)));
 unsigned s263=nr_bits((nr_float(s254)*nr_float(s260)));
 unsigned s264=nr_bits((nr_float(s255)*nr_float(s261)));
 unsigned s265=nr_bits(nr_max(nr_float(s250),nr_float(s262)));
 unsigned s266=nr_bits(nr_max(nr_float(s251),nr_float(s263)));
 unsigned s267=nr_bits(nr_max(nr_float(s252),nr_float(s264)));
 unsigned s268=nr_bits(nr_max(nr_float(s266),nr_float(s265)));
 unsigned s269=nr_bits(nr_max(nr_float(s267),nr_float(s268)));
 unsigned s270=nr_bits(nr_max(nr_float(s269),nr_float(0u)));
 unsigned s271=nr_bits(((nr_float(s270)*nr_float(s238))+nr_float(s232)));
 unsigned s272=nr_bits(((nr_float(s270)*nr_float(s239))+nr_float(s233)));
 unsigned s273=nr_bits(((nr_float(s270)*nr_float(s240))+nr_float(s234)));
 unsigned s274=nr_bits((nr_float(s214)*nr_float(s271)));
 unsigned s275=nr_bits((nr_float(s214)*nr_float(s272)));
 unsigned s276=nr_bits((nr_float(s214)*nr_float(s273)));
 unsigned s277=nr_bits(((nr_float(s274)*nr_float(constant[7]))+nr_float(s210)));
 unsigned s278=nr_bits(((nr_float(s275)*nr_float(constant[7]))+nr_float(s211)));
 unsigned s279=nr_bits(((nr_float(s276)*nr_float(constant[7]))+nr_float(s212)));
 unsigned s280=nr_bits((nr_float((s214^0x80000000u))+nr_float(1065353216u)));
 unsigned s281=nr_bits(nr_saturate(nr_float(nr_bits(((nr_float(constant[4])*nr_float(s280))+nr_float(s277))))));
 unsigned s282=nr_bits(nr_saturate(nr_float(nr_bits(((nr_float(constant[5])*nr_float(s280))+nr_float(s278))))));
 unsigned s283=nr_bits(nr_saturate(nr_float(nr_bits(((nr_float(constant[6])*nr_float(s280))+nr_float(s279))))));
 unsigned s284=nr_bits((nr_float(s132)*nr_float(1017370378u)));
 unsigned s285=nr_bits((nr_float(s133)*nr_float(1017370378u)));
 unsigned s286=nr_bits((nr_float(s134)*nr_float(1017370378u)));
 unsigned s287=nr_bits((nr_float(s135)*nr_float(1017370378u)));
 unsigned s288=nr_bits(((nr_float((s135^0x80000000u))*nr_float(1017370378u))+nr_float(1065353216u)));
 unsigned s289=nr_bits((nr_float(s214)+nr_float(constant[7])));
 unsigned s290=nr_bits(((nr_float((s214^0x80000000u))*nr_float(constant[7]))+nr_float(s289)));
 unsigned s291=nr_bits(((nr_float(s288)*nr_float(s281))+nr_float(s284)));
 unsigned s292=nr_bits(((nr_float(s288)*nr_float(s282))+nr_float(s285)));
 unsigned s293=nr_bits(((nr_float(s288)*nr_float(s283))+nr_float(s286)));
 unsigned s294=nr_bits(((nr_float(s288)*nr_float(s290))+nr_float(s287)));
 unsigned s295=nr_bits((nr_float(s291)*nr_float(input[7])));
 unsigned s296=nr_bits((nr_float(s292)*nr_float(input[7])));
 unsigned s297=nr_bits((nr_float(s293)*nr_float(input[7])));
 unsigned s298=nr_bits((nr_float(s294)*nr_float(input[7])));
 color[0]=s295;
 color[1]=s296;
 color[2]=s297;
 color[3]=s298;
}

typedef unsigned uint4 __attribute__((ext_vector_type(4)));
typedef float float4 __attribute__((ext_vector_type(4)));
INLINE unsigned u32(const unsigned char *p){return (unsigned)p[0]|((unsigned)p[1]<<8)|((unsigned)p[2]<<16)|((unsigned)p[3]<<24);}
INLINE float bounded(float v,float hi){return v<0?0:(v>hi?hi:v);}
INLINE float4 texel(const unsigned char *p,unsigned format){
 uint4 raw=((uint4)u32(p)>>(uint4){0,8,16,24})&(uint4)255;
 float4 value=__builtin_convertvector(raw,float4)/255.0f;
 return format==87?value.zyxw:value;
}
INLINE float4 mix4(float4 a,float4 b,float t){return a*(1.0f-t)+b*t;}
void nr_probe_kernel(const unsigned char *params){
 if(!params)return;
 for(unsigned n=0;n<2;++n){const unsigned char *p=params+n*40;
  if(!u32(p)||u32(p+4)||u32(p+8)!=1||u32(p+12)!=4||!u32(p+16)||!u32(p+20)||u32(p+24)!=2||u32(p+28)!=0x4321||u32(p+32)||u32(p+36)!=2)return;
  const unsigned *dims=(const unsigned*)u32(p+16);if(dims[0]!=131072||dims[1]!=1||dims[2]!=1||dims[3]!=1)return;
 }
 const unsigned char *data=(const unsigned char*)u32(params);unsigned char *output=(unsigned char*)u32(params+40);
 uint4 *words=(uint4*)output;for(unsigned n=0;n<16384;++n)words[n]=(uint4)0;
 unsigned phase=u32(data+8),count=u32(data+12);
 if(u32(data)!=0x4e524331||u32(data+4)!=1||phase>1||count>1024||u32(data+16)!=262144||u32(data+20)||u32(data+24)||u32(data+28))return;
 unsigned width[2],height[2],format[2],address_u[2],address_v[2],filter[2];
 if(u32(data+56)||u32(data+60))return;
 for(unsigned n=0;n<2;++n){width[n]=u32(data+256+n*12);height[n]=u32(data+260+n*12);format[n]=u32(data+264+n*12);
  address_u[n]=u32(data+32+n*8);address_v[n]=u32(data+36+n*8);filter[n]=u32(data+48+n*4);
  if(!width[n]||width[n]>8192||!height[n]||height[n]>8192||(format[n]!=28&&format[n]!=87)||width[n]*height[n]>4194304||address_u[n]>1||address_v[n]>1||filter[n]>1)return;
 }
 const unsigned *constant=(const unsigned*)(data+64);
 for(unsigned n=0;n<24;++n)if((constant[n]&0x7f800000u)==0x7f800000u)return;
 #pragma clang loop unroll(disable)
 for(unsigned pixel=0;pixel<count;++pixel){
  const unsigned char *record=data+512+pixel*192;const unsigned *input=(const unsigned*)record;
  for(unsigned n=0;n<16;++n)if((input[n]&0x7f800000u)==0x7f800000u)return;
  if(!phase){
   unsigned coordinates[10];nr_coordinates(input,constant,(const unsigned*)0,coordinates);
   unsigned *result=(unsigned*)(output+16+pixel*128);
   for(unsigned sample=0;sample<5;++sample){unsigned slot=sample?1:0,w=width[slot],h=height[slot];
    float u=nr_float(coordinates[sample*2]),v=nr_float(coordinates[sample*2+1]);
    if((nr_bits(u)&0x7f800000u)==0x7f800000u||(nr_bits(v)&0x7f800000u)==0x7f800000u)return;
    if(address_u[slot]){u=nr_float(nr_bits(u)&0x7fffffffu);u=u-nr_floor(u*.5f)*2.0f;if(u>1.0f)u=2.0f-u;}
    if(address_v[slot]){v=nr_float(nr_bits(v)&0x7fffffffu);v=v-nr_floor(v*.5f)*2.0f;if(v>1.0f)v=2.0f-v;}
    float x=bounded(bounded(u,1.0f)*(float)w-0.5f,(float)(w-1));
    float y=bounded(bounded(v,1.0f)*(float)h-0.5f,(float)(h-1));
    unsigned ix=(unsigned)x,iy=(unsigned)y,jx=ix+1<w?ix+1:ix,jy=iy+1<h?iy+1:iy;
    if(filter[slot]){ix=(unsigned)bounded(bounded(u,1.0f)*(float)w,(float)(w-1));iy=(unsigned)bounded(bounded(v,1.0f)*(float)h,(float)(h-1));jx=ix;jy=iy;x=(float)ix;y=(float)iy;}
    result[sample*4]=iy*w+ix;result[sample*4+1]=iy*w+jx;result[sample*4+2]=jy*w+ix;result[sample*4+3]=jy*w+jx;
    result[20+sample*2]=nr_bits(x-(float)ix);result[21+sample*2]=nr_bits(y-(float)iy);
   }
  }else{
   unsigned sampled[20];
   for(unsigned sample=0;sample<5;++sample){unsigned slot=sample?1:0;
    float tx=nr_float(u32(record+64+sample*8)),ty=nr_float(u32(record+68+sample*8));
    if(!(tx>=0&&tx<1&&ty>=0&&ty<1))return;
    const unsigned char *p=record+104+sample*16;
    float4 value=filter[slot]?texel(p,format[slot]):mix4(mix4(texel(p,format[slot]),texel(p+4,format[slot]),tx),mix4(texel(p+8,format[slot]),texel(p+12,format[slot]),tx),ty);
    for(unsigned c=0;c<4;++c)sampled[sample*4+c]=nr_bits(value[c]);
   }
   nr_color(input,constant,sampled,(unsigned*)(output+16+pixel*16));
  }
 }
 unsigned *header=(unsigned*)output;header[1]=phase;header[2]=count;header[3]=0;header[0]=0x4e52434f;
}

void *memset(void *destination,int value,__SIZE_TYPE__ count){volatile unsigned char *p=(volatile unsigned char*)destination;for(__SIZE_TYPE__ i=0;i<count;++i)p[i]=(unsigned char)value;return destination;}
