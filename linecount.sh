#!/bin/bash
cat $(find ./src/ -type f | egrep -i "(.h|.c)$") | wc -l
