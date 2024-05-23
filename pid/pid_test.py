import csv
import matplotlib.pyplot as plt
import numpy as np
import sys

import argparse
parser = argparse.ArgumentParser()

parser.add_argument("-p","--kp",type=float,default=2.0)
parser.add_argument("-i","--ki",type=float,default=0.0)
parser.add_argument("-d","--kd",type=float,default=0.0)
parser.add_argument("-c","--cmd_angle",type=float,default=30.0)
parser.add_argument("-s","--start_angle",type=float,default=0.0)
parser.add_argument("-o","--offset",type=float,default=0.0)
parser.add_argument("-l","--loop_time",type=float,default=30.0)

plot_colors = ["b","g","k","r","c","y"]
args= parser.parse_args()
data = {}

angle = args.start_angle 
loop_speed = 0.001
cmd_angle = args.cmd_angle
steer_angle = 0.0
actual_angle =0.0
steer_angle_offset = args.offset
port_starbord_adjust =0.0

loop_time = args.loop_time
Pi = 0.0
e_prev = 0.0

plot_data= {}
plot_data["angle"]=[]
plot_data["steer_angle"]=[]
plot_data["cmd_angle"]=[]
plot_data["actual_angle"]=[]
plot_data["Pi"]=[]
plot_data["time"]=[]
plot_data["D"]=[]
Kp=args.kp
Ki=args.ki
Kd=args.kd

D=0
motor_speed = 10/1   #degrees per second  steering angle

def pid_loop(Kp,Ki,Kd,cmd_angle,angle,time):
    global Pi,e_prev,D
    #angle = angle*0x10000/360.0;
    #cmd_angle = cmd_angle*0x10000/360.0;
    e = cmd_angle-angle
    #print("error" , e)
    if e > 180.0 :
        e =  e-360
    if e < -180.0 :
        e =  e+360
    #print("error" , e)
    P = Kp*e
    if abs(e)>10.0 :
        Ki=0.0
        Kd=0.0
    Pi = Pi + Ki*e*(time)
    D = Kd*(e-e_prev)/time
    change_angle = P + Pi + D
    #print("change_angle" , e)
    e_prev = e
    #return change_angle*360.0/0x10000
    return change_angle


for time in np.arange(0,loop_time,loop_speed):
    plot_data["time"].append(time)
    plot_data["angle"].append(angle)
    #plot_data["steer_angle"].append(steer_angle)
    plot_data["cmd_angle"].append(cmd_angle)
    plot_data["actual_angle"].append(actual_angle)
    #plot_data["Pi"].append(Pi)
    #plot_data["D"].append(D)
    if actual_angle < steer_angle :
        if steer_angle-actual_angle > motor_speed*loop_speed :
            actual_angle += motor_speed*loop_speed
        else :
            actual_angle = steer_angle
    if actual_angle > steer_angle :
        if actual_angle-steer_angle > motor_speed*loop_speed :
            actual_angle -= motor_speed*loop_speed
        else :
            actual_angle = steer_angle

    angle += actual_angle*loop_speed 
    if(angle<0) : angle +=360.0
    if(angle>360) : angle -=360.0
    steer_angle = pid_loop(Kp,Ki,Kd,cmd_angle,angle,loop_speed)+steer_angle_offset
    #print("steer angle",steer_angle," angle " , angle)


print("done")
    
#plt.legend()
for i in plot_data.keys():
#    print ("Is time ",i,i=="time")
    if len(plot_data[i]) > 0 and i!="time":
        plt.plot(plot_data["time"],plot_data[i],label=i)


plt.title("Test pid")
plt.legend()
plt.show()
