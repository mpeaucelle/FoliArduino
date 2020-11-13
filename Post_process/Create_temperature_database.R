rm(list=ls())
workdir<-"directory with data"
setwd(workdir)
fic<-list.files(pattern = ".TXT")

all_data<-NULL

# Step 1 - We merge all data

for ( i in fic ){
  tmp<-read.table(file = i, sep=";", header = FALSE)
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

# # Approach 1: Steinhart–Hart equation: 1/T = A + Bln(R) + C(ln(R))3
# # coefficients can be estimated by fitting 3 measurements. 
# c1 = 1.009249522e-03
# c2 = 2.378405444e-04
# c3 = 2.019202697e-07
# Tk = (1.0 / (c1 + c2*log(R2) + c3*log(R2)*log(R2)*log(R2)))
# Cold_Tc = Tk - 273.15 # Cold temperature in °C

# Approach 2: simplified version of Steinhart–Hart equation : 1/T = 1/Tref + (1/B) ⋅ ln (R/Rref)
# use B value from thermistor datasheet
# https://asset.conrad.com/media10/add/160267/c1/-/en/000500682DS01/fiche-technique-500682-thermistance-ntc-tdk-b57861s103f40-s861-1-pcs.pdf
B = 3988.0 # +/- 0.3%
Tref = 298.15 # 25°C
Rref = 10000.0 # 10kohms
Tk = (B * Tref) / (B + (Tref * log(R2 / Rref)))
Cold_Tc = Tk - 273.15 # Cold temperature in °C

# Step 3 - We convert Cold_Tc in voltage at the cold junction


# Step 4 - We calculate 






write.table(x=all_data,file="Bud_temperature.txt",col.names = TRUE,sep=";")

