cmake --build --preset debug
cd /home/caleb/projects/trraform-build-processor/scripts
source .venv/bin/activate
python test.py
deactivate
cd /home/caleb/projects/trraform-build-processor
/home/caleb/projects/trraform-build-processor/build/debug/trrasvr