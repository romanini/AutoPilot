#include <Wire.h>

float Pi;
float e_prev;
float Kp,Ki,Kd;

void setup_pid() {
	Pi=0.0;
	e_prev = 0.0;
	Kp =2.0;
	Ki = 0.0;
	Kd = 0.0;
}

float pid_loop(float target,float current,float time) {
	
	float e = target-current;
	if (e>180.0) e= e-360.0;
	if (e<-180.0) e=e+360.0;
	float P=Kp*e;
	float l_ki,l_kd;
	l_ki = Ki;
	l_kd = Kd;
        if(abs(e)>10.0)
	{
		l_ki=0.0;
		l_kd=0.0;
	}
	Pi = Pi + l_ki*e*time;
	float D = (l_kd*e-e_prev)/time;
	float change_angle = P + Pi + D;
	e_prev = e;

	return change_angle;
	


}
