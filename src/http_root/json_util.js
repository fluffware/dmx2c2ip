function createRequest() {
    var request;
   
    request = new HttpRequest();

    if (!request)
	alert("Error initializing XMLHttpRequest!");
    return request;
}

function requestValues(url)
{
    jQuery.ajax({url:url, accepts:'application/json',success:handleValueReply});
}


function handleValueReply(data,status,req)
{
    alert("Done"); 
}