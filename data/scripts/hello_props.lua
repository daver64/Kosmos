-- Manual script: press Run. Lists a few properties; `props` is the property tree.
print("ship: " .. tostring(props.get("/sim/active-ship")))
print("throttle: " .. tostring(props.get("/controls/throttle")))
print("warp index: " .. tostring(props.get("/sim/warp-index")))
