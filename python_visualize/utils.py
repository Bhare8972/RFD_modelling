#!/usr/bin/python
import numpy as np
from matplotlib import pyplot as plt
from mpl_toolkits.mplot3d import Axes3D
from constants import *

class particle_history(object):
	def __init__(self, id):
		self.id=id
		self.pos_history=[]
		self.mom_history=[]
		
	def add_position(self, new_pos):
		self.pos_history.append(new_pos)
		
	def add_momentum(self, new_mom):
		self.mom_history.append(new_mom)
		
	def get_X(self):
		return np.array([p[0] for p in self.pos_history])
		
	def get_Y(self):
		return np.array([p[1] for p in self.pos_history])
		
	def get_Z(self):
		return np.array([p[2] for p in self.pos_history])
		
def read_text_A(fname):
	with open(fname, 'rt') as fin:
		data=fin.read().split()
		
	data_i=0
	particles={}
	while data_i<len(data):
		itter_n=int(data[data_i])
		num_particles=int(data[data_i+1])
		data_i+=2
		for particle_index in xrange(num_particles):
			particle_ID=int(data[data_i])
			particle_pos=np.zeros(3)
			particle_pos[0]=float(data[data_i+1])
			particle_pos[1]=float(data[data_i+2])
			particle_pos[2]=float(data[data_i+3])
			particle_mom=np.zeros(3)
			particle_mom[0]=float(data[data_i+4])
			particle_mom[1]=float(data[data_i+5])
			particle_mom[2]=float(data[data_i+6])
			data_i+=7
			
			if not particle_ID in particles:
				particles[particle_ID]=particle_history(particle_ID)
				
			particles[particle_ID].add_position(particle_pos)
			particles[particle_ID].add_momentum(particle_mom)
			
	return particles
				
	
if __name__=='__main__':
	particle_data=read_text_A('./output.txt')
	p_final=particle_data[1].mom_history[-1][2]
	gamma=np.sqrt(1+np.sum(p_final*p_final))
	p_conv=p_final*electron_rest_mass/C
	print 'momentum:', p_conv
	print 'gamma:', gamma
	print 'velocity:', p_conv/(gamma*electron_mass)
	pos_final=particle_data[1].pos_history[-1][2]*distance_units
	print 'position:', pos_final
	time=len(particle_data[1].pos_history)*0.1*time_units
	print 'time:',time
	print
	print
	
	accel=1.0e-4*elementary_charge/electron_mass
	final_velocity=accel*time
	final_position=0.5*final_velocity*time+1
	print 'classical velocity:', final_velocity
	print 'classical position:', final_position
	
	fig = plt.figure()
	ax = fig.gca(projection='3d')
	ax.plot(particle_data[1].get_X()*distance_units, particle_data[1].get_Y()*distance_units, particle_data[1].get_Z()*distance_units)
	plt.show()
	
	
	
