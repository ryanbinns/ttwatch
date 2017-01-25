#!/usr/bin/python

""" With this script elevation data can be added to a gpx file. To do so,
    go to http://www.gpsvisualizer.com/elevation and create a gpx file with
    elevation data from your gpx file w/o elevation data. Then call this
    script with both filenames as arguments. The gpx file with elevation data
    will be written to stdout.
"""

import sys

def usage():
  print >>sys.stderr, "Usage: " + sys.argv[0] + " <file with elevation data> <file w/o elevation data>"

def read_elevation_from_file(filename):
  elevation_data={} # leeres Dictionary
  f = open(filename, "r")

  for line in f:
    if line.find("<trkpt") >= 0:
      point_a=line.strip(" <>\r\n").split()
      point=point_a[0] + " " + point_a[2].strip("0\"") + " " + point_a[1].strip("0\"")
    if line.find("<ele>") >= 0:
      elev=line.strip()
      elevation_data[point]=elev
  f.close()
  return elevation_data

def append_elevation_to_file(data, filename):
  f = open(filename, "r")

  for line in f:
    sys.stdout.write(line) # print w/o newline
    if line.find("<trkpt") >= 0:
      point_a=line.strip(" <>\r\n").split()
      point=point_a[0] + " " + point_a[1].strip("0\"") + " " + point_a[2].strip("0\"")
      print data[point] # newline was stripped before

  f.close()

if __name__ == "__main__":
  argc = len(sys.argv)

  if argc < 3:
    usage()
    sys.exit(1)

  elevation_data = read_elevation_from_file(sys.argv[1])
  if len(elevation_data) == 0:
    print "Keine Hoehendaten gefunden."
    exit(2)

  #print elevation_data

  append_elevation_to_file(elevation_data, sys.argv[2])
  exit(0)
