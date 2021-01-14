###############################################################################################
# Script: Create_temperature_databse.R
# Author: Marc Peaucelle
# Date: 01/2021
# Description: This script convert thermistance and thermocouple voltage values to temperature
# Project: Foliarduino - Bud temperature measurement with type-T thermocouples
# Check https://foliarduino.com/t-type-thermocouples-for-bud-temperature-monitoring/ for a 
# description of the device
###############################################################################################
library(xts)
library(dplyr)
rm(list=ls())
workdir<-"directory with data"
workdir<-"/media/marc/Store/Science_projects/FoliArduino/Tbud_data/"

setwd(workdir)
fic<-list.files(pattern = ".TXT")

all_data<-NULL

# Step 1 - We merge all data

for ( i in fic ){
  tmp<-read.table(file = i, sep=";", header = TRUE)
  year<-rep(as.numeric(substr(i,start = 1,stop=4)),4)
  month<-rep(as.numeric(substr(i,start = 5,stop=6)),4)
  day<-rep(as.numeric(substr(i,start = 7,stop=8)),4)
  hour<-rep(as.numeric(substr(i,start = 10,stop=11)),4)
  min<-rep(as.numeric(substr(i,start = 12,stop=13)),4)
  sec<-rep(as.numeric(substr(i,start = 14,stop=15)),4)
  idate<-strptime(substr(i,start=1,stop=15),format="%Y%m%d_%H%M%S") # convert file name to date
  tmp<-cbind(file=rep(i,4),year,month,day,hour,min,sec,tmp)
  all_data<-rbind(all_data,tmp)  
}

# Step 2 - we compute cold junction temperature from thermistance
Cold_Vo<-all_data$Cold_T # encoded on 1024 bits 
# resistor used in serie with the thermistor
R1 = 10000 #ohm

# The arduino encode analogread on 1024 bits (from 0 to 1023)
# it means that 1 bit corresponds to a sensitivity of 5V / 1024 = 0.00488 V
# Computes thermistor value 
# Vout = Vin * (R2/(R1+R2)) with Vin = 5V from the arduino
R2 = R1 * (1023.0 / Cold_Vo - 1.0)


# multiple approaches can be used to estimate temperature from a thermistor
# see https://www.digikey.com/en/maker/projects/how-to-measure-temperature-with-an-ntc-thermistor/4a4b326095f144029df7f2eca589ca54

# # Approach 1: Steinhart-Hart equation: 1/T = A + Bln(R) + C(ln(R))3
# # coefficients can be estimated by fitting 3 measurements --> Calibration should be done for each Thermocouple
# c1 = 1.009249522e-03
# c2 = 2.378405444e-04
# c3 = 2.019202697e-07
# Tk = (1.0 / (c1 + c2*log(R2) + c3*log(R2)*log(R2)*log(R2)))
# Tcj = Tk - 273.15 # Cold temperature in °C

# # Approach 2: simplified version of Steinhart-Hart equation : 1/T = 1/Tref + (1/B) + ln (R/Rref)
# # use B value from thermistor datasheet
# # https://asset.conrad.com/media10/add/160267/c1/-/en/000500682DS01/fiche-technique-500682-thermistance-ntc-tdk-b57861s103f40-s861-1-pcs.pdf
B = 3988.0 # +/- 0.3%
Tref = 298.15 # 25°C
Rref = 10000.0 # 10kohms
Tk = (B * Tref) / (B + (Tref * log(R2 / Rref)))
Tcj = Tk - 273.15 # Cold temperature in °C

# Step 3 - We convert Cold_Tc into voltage at the cold junction
# lots of information here: http://www.mosaic-industries.com/embedded-systems/microcontroller-projects/temperature-measurement/thermocouple/cold-junction-compensation
# information on converting temperature to voltage here: http://www.mosaic-industries.com/embedded-systems/microcontroller-projects/temperature-measurement/thermocouple/calibration-table#computing-cold-junction-voltages
# The following table is of calibration coefficients for Type T thermocouple wires.
# In our case we are only interested in temperatures between -20 and 70?C corresponding to a range of -0.757 to 2.909 mV
To<- 2.5000000E+01
Vo<-	9.9198279E-01
p1<-	4.0716564E-02
p2<-	7.1170297E-04
p3<-	6.8782631E-07
p4<-	4.3295061E-11
q1<-	1.6458102E-02
q2<-	0.0000000E+00

Tdif=Tcj-To
num<-Tdif*(p1+Tdif*(p2+Tdif*(p3+p4*Tdif)))
denom<-1+Tdif*(q1+q2*Tdif)

denom[denom==0]<-0.00000000000001 # avoid division 0
Vcj = Vo + num/denom # in mV

# Step 4 - We calculate Bud temperature from the hot junction voltage
# real thermocouple compensated voltage

all_data$Hot_T<-all_data$Hot_T/1000 ###### convert from ?V to mV

Vtc<- all_data$Hot_T + Vcj


# The following table is of calibration coefficients for Type T thermocouple wires.
# http://www.mosaic-industries.com/embedded-systems/microcontroller-projects/temperature-measurement/thermocouple/type-t-calibration-table
#                                                   Range
# Voltage:	    -6.18 to -4.648 mV	| -4.648 to 0 mV	| 0 to 9.288 mV	| 9.288 to 20.872 mV
# Temperature:	-250 to -150?C	    | -150 to 0?C	    | 0 to 200?C	  | 200 to 400?C
#                                              Coefficients
# T0	          -1.9243000E+02	    | -6.0000000E+01	| 1.3500000E+02	| 3.0000000E+02
# V0	          -5.4798963E+00	    | -2.1528350E+00	| 5.9588600E+00	| 1.4861780E+01
# p1	          5.9572141E+01	      | 3.0449332E+01	  | 2.0325591E+01	| 1.7214707E+01
# p2	          1.9675733E+00	      | -1.2946560E+00	| 3.3013079E+00	| -9.3862713E-01
# p3	          -7.8176011E+01	    | -3.0500735E+00	| 1.2638462E-01	| -7.3509066E-02
# p4	          -1.0963280E+01	    | -1.9226856E-01	| -8.2883695E-04|	2.9576140E-04
# q1	          2.7498092E-01	      | 6.9877863E-03	  | 1.7595577E-01	| -4.8095795E-02
# q2	          -1.3768944E+00	    | -1.0596207E-01	| 7.9740521E-03	| -4.7352054E-03
# q3	          -4.5209805E-01	    | -1.0774995E-02	| 0.0           |	0.0

Vbreaks<-c(-4.648,0,9.288)
To<-c(	-1.9243000E+02,	-6.0000000E+01,	1.3500000E+02,	3.0000000E+02)
Vo<-c(	-5.4798963E+00,	-2.1528350E+00,	5.9588600E+00,	1.4861780E+01)
p1<-c(	5.9572141E+01,	3.0449332E+01,	2.0325591E+01,	1.7214707E+01)
p2<-c(	1.9675733E+00,	-1.2946560E+00,	3.3013079E+00,	-9.3862713E-01)
p3<-c(	-7.8176011E+01,	-3.0500735E+00,	1.2638462E-01,	-7.3509066E-02)
p4<-c(	-1.0963280E+01,	-1.9226856E-01,	-8.2883695E-04,	2.9576140E-04)
q1<-c(	2.7498092E-01,	6.9877863E-03,	1.7595577E-01,	-4.8095795E-02)
q2<-c(	-1.3768944E+00,	-1.0596207E-01,	7.9740521E-03,	-4.7352054E-03)
q3<-c(	-4.5209805E-01,	-1.0774995E-02,	0.0,	0.0)

index<-NULL
index[Vtc<Vbreaks[1]]<-1
index[(Vtc>Vbreaks[1])&(Vtc<Vbreaks[2])]<-2
index[(Vtc>Vbreaks[2])&(Vtc<Vbreaks[3])]<-3
index[Vtc>Vbreaks[3]]<-4

Vdif<-Vtc-Vo[index]
num<-Vdif*(p1[index]+Vdif*(p2[index]+Vdif*(p3[index]+p4[index]*Vdif)))
denom<-1+Vdif*(q1[index]+Vdif*(q2[index]+q3[index]*Vdif))

denom[denom==0]<-0.00000000000001 # avoid division 0

Thj<-To[index] + num/denom


all_data<-cbind(all_data,R2,Tcj,Vcj,Vtc, Thj)
write.table(x=all_data,file="Bud_temperature.txt",col.names = TRUE,sep=";")

############################ Plot Tbud, Tair and Tdif = Tbud-Tair for each Sensor
# Function to plot Tbud, Tair and Tdif (Tbud-Tair)
plot.temp<-function(Thermo="Thermocouple_1",filt=FALSE){ # filt = TRUE --> remove values with thermocouple fault detected
  if (filt){
    Tsel<-filter(all_data,Sensor==Thermo & Fault==0)
  } else {
    Tsel<-filter(all_data,Sensor==Thermo)
  }
  tstep<-as.POSIXlt(substr(Tsel$file,start=1,stop=15),format="%Y%m%d_%H%M%S")
  
  Tbud<-xts(cbind(Tair=Tsel$Tcj,
                  Tbud=Tsel$Thj,
                  Tdif=Tsel$Thj-Tsel$Tcj),tstep)
  print(plot(Tbud))
  return(Tbud)
}

Tbud1<-plot.temp("Thermocouple_1")
Tbud2<-plot.temp("Thermocouple_2")
Tbud3<-plot.temp("Thermocouple_3")
Tbud4<-plot.temp("Thermocouple_4")

# Plot a specific period
plot(Tbud1["2021-01-10/2021-01-11"])

