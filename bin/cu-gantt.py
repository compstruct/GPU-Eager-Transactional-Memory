#!/usr/bin/python -O

import sys
import os

if not os.environ['HOME']:
	print 'please set your HOME environment variable to your home directory'
	sys.exit
if not os.environ['GPGPUSIM_ROOT']:
	print 'please set your GPGPUSIM_ROOT environment variable to your home directory'
	sys.exit

sys.path.append( os.environ['GPGPUSIM_ROOT'] + '/aerialvision/' ) 

import Tkinter as Tk
import Pmw
import time

from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg, NavigationToolbar2TkAgg
from matplotlib.figure import Figure

import matplotlib.pyplot as plt

class cu_timestamp:

   def __init__(self, color):
      self.series = []
      self.color = color
      self.max = 0
      self.min = 0

   def add_data(self, data):
      self.series.append(data)
      self.max = max(data, self.max)
      if (self.min != 0):
         self.min = min(data, self.min)
      else:
         self.min = data

class cu_ganttchart:

   def __init__(self, filename, min_id=1, max_id=1000, xscale=0):
      self.filename = filename
      self.sum = 0
      self.timestamps = {
         'AC' : cu_timestamp('white'), 
         'FL' : cu_timestamp('pink'), 
         'VW' : cu_timestamp('red'), 
         'RW' : cu_timestamp('orange'), 
         'PF' : cu_timestamp('yellow'), 
         'AW' : cu_timestamp('green'), 
         'CR' : cu_timestamp('cyan'), 
         'CS' : cu_timestamp('blue'), 
         'RT' : cu_timestamp('purple') 
      }
      self.min_id = min_id
      self.max_id = max_id
      self.xscale = xscale 
      self.commit_ids = []
      print "parsing '%s'; commit_id = (%d to %d)" % (filename, min_id, max_id)
      datafile = open(filename, 'r')
      for line in datafile:
         commit_id = self.parse_line(line)
         if (commit_id > max_id):
            break
      datafile.close()


   def parse_line(self, line):
      token = line.split()
      commit_id = int(token[0])
      if commit_id < self.min_id or commit_id > self.max_id:
         return commit_id

      self.sum += commit_id

      for x in range(3, 12): 
         ts_str = token[x].split('=')
         timestamp = int(ts_str[1])
         self.timestamps[ts_str[0]].add_data(timestamp)

      # fill in the zero time stamp with timestamp that reflect the state 
      # (any zero will be set to the timestamp of its next state) 
      state_list = ('RT', 'CS', 'CR', 'AW', 'PF', 'RW', 'VW', 'FL', 'AC')
      next_state_time = self.timestamps[state_list[0]].series[-1]
      for state in state_list:
         state_time = self.timestamps[state].series[-1]
         if (state_time == 0): 
            state_time = next_state_time 
            self.timestamps[state].series[-1] = next_state_time 
         next_state_time = state_time 

      self.commit_ids.append(commit_id)

      return commit_id


   def plot(self):
      print self.sum

      ind = self.commit_ids 
      width = 1

      plots = []
      xmin = 0
      xmax = 0
      state_list = ('RT', 'CS', 'CR', 'AW', 'PF', 'RW', 'VW', 'FL', 'AC')
      for state in state_list:
         state_ts = self.timestamps[state]
         print "%s, (%d,%d)" % (state, state_ts.min, state_ts.max)
         plots.append( plt.barh(ind, self.timestamps[state].series, width, color=self.timestamps[state].color, linewidth=0, align='center') )
         xmax = max(xmax, state_ts.max)
         if (state_ts.min != 0):
            if (xmin == 0):
               xmin = state_ts.min
            else:
               xmin = min(xmin, state_ts.min)

      print xmin, xmax
      if (self.xscale == 0): 
         plt.xlim(xmin, xmax)
      else:
         plt.xlim(xmin, xmin + self.xscale)
      plt.ylim(self.min_id, self.max_id)

      state_list_legend = state_list[1:]
      plt.figlegend( [px[0] for px in plots[:-1]], state_list_legend, loc='lower right' )

      title = self.filename[-50:]
      plt.title(title)

      plt.show()


if (len(sys.argv) > 4):
   gchart = cu_ganttchart(sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4]))
else:
   gchart = cu_ganttchart(sys.argv[1], int(sys.argv[2]), int(sys.argv[3]))
gchart.plot()

