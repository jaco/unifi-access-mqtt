# Development

```
  To Deploy                                                                                                    
                                                                                                               
  1. Build firmware:                                                                                           
  pio run -e esp32-poe        # For Ethernet board                                                             
  pio run -e esp32-s3-zero    # For WiFi board                                                                 
  2. Upload filesystem (first time or after UI changes):                                                       
  pio run -t uploadfs -e esp32-poe                                                                             
  3. Upload firmware:                                                                                          
  pio run -t upload -e esp32-poe                                                                               
  4. Access web UI:                                                                                            
    - http://doorbell.local or device IP on port 80                                                            
    - For unconfigured WiFi boards: connect to "UniFi-Doorbell-XXXX" AP (password: doorbell123)     
```
