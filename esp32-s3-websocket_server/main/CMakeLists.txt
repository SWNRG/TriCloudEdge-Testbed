#this kind of setup and inclusion seems to be manual and far from optimal 
#in theory, the idf_components.yml should do a more clear automatic work
# never tested though

idf_component_register(SRCS
	"main.cpp"
	"mqtt.c"
	"wifi.c"
	"websocket_server.cpp"
	"image_processor.cpp"
	"face_recognizer.cpp"
	"face_database.c"
	"storage_manager.c"
	"face_enroller.cpp"
    INCLUDE_DIRS
        "."
    EMBED_TXTFILES
        "../certificates/AmazonRootCA1.pem"
        "../certificates/new_certificate.pem"
        "../certificates/new_private.key"
	REQUIRES
        esp-dl                
        human_face_detect      
        human_face_recognition
		esp_netif 
        esp_http_server      
        json 
		nvs_flash
		spiffs
	PRIV_REQUIRES 
		esp_wifi 
		mqtt	
)
target_add_binary_data(${COMPONENT_TARGET} "faces.json" TEXT)