function createRequest() {
    var request;
   
    request = new HttpRequest();

    if (!request)
	alert("Error initializing XMLHttpRequest!");
    return request;
}

function requestValues(url)
{
    jQuery.getJSON(url, handleValueReply).error(requestError);
}


function handleValueReply(data,status,req)
{
    $("#result").text(JSON.stringify(data));
}

function requestError(req,status,error)
{
    alert("Request failed ("+status+","+error+")");
}

function sendValues(url, data)
{
    jQuery.ajax({type:"POST", url:url, dataType:"json", mimeType:"application/json", data:JSON.stringify(data), success:handleValueReply, error:postError});
}

function postError(req,status,error)
{
    alert("Post request failed ("+status+","+error+")");
}
