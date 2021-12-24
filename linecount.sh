#!/bin/bash
cat $(find ./ -type f | egrep -i "(.h|.c)$") | wc -l
