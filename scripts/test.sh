cmake --build --preset arm-debug
source .venv/bin/activate
python ./test.py
deactivate
cd ..
/Users/caleballen/Desktop/Projects/trraform/trraform-build-processor/build/arm-debug/trrasvr