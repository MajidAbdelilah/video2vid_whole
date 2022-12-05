
function getStatus() {
   $.get("http://0.0.0.0:8080/status", function(data, status){
	   if(status == "success"){
		   if(data == "n"){
			   console.log("video process is done = no");
			   window.setTimeout("getStatus()",500)
    	   }else if(data == "y"){
			   console.log("video process is done = yes")
			   return;
		   }
	   }
   });
}

$('#myform').submit(function(e){
    //e.preventDefault();

	//binds to onchange event of your input field
	//$('#video_file').files[0].size

		//this.files[0].size gets the size of your file.
	//	alert($('#video_file')[0].files[0].size);
		if($('#video_file')[0].files[0].size>100000000){
			//e.preventDefault();
		}
	
	

	//getStatus();
	// $.getJSON("http://0.0.0.0:8080/status",{},//{"session":0, "requestID":12345}, 
    //function(data) { //data is the returned JSON object from the server {name:"value"}
          //setStatus(data.status);
	//	console.log(data.status);
	//});
    // do ajax now
    //console.log("submitted"); 
});

