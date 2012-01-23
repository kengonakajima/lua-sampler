sampler = require "luasampler"

for i=1,1000 do
--   local data =
   print("i:",i)
      sampler.debug()
--   print("ndata:",#data)
end



if false then 
   local nChan = 1
   local freq = 44100
   local recorder = sampler.createRecorder(nChan,freq)


   recorder:prepareBuffer(0.2)

   recorder:start()

   local intv = 0.1
   local startAt = os.time()
   local nextStopAt = startAt + intv
   local loopcnt = 0
   while true do
      loopcnt = loopcnt + 1
      
      local curTime = os.time()
      
      if curTime > nextStopAt then
         nextStopAt = curTime + intv
         print("reading..")
         local data = recorder:readBuffer()
         print("done. data:", data )
         if data then print( data[1], data[2], data[3], data[10] ) end
         local data = recorder:readBuffer()
         print("done. data:", data )
         local data = recorder:readBuffer()
         print("done. data:", data )
         
      end
   end
   print("finished")

end
