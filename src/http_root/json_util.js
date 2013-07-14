
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
    jQuery.ajax({type:"POST", url:url, dataType:"json", contentType:"application/json", data:JSON.stringify(data), success:handleValueReply, error:postError});
}

function postError(req,status,error)
{
    alert("Post request failed ("+status+","+error+")");
}

function FieldUpdate(url_arg, top_arg)
{
    var values;
    var top = top_arg;
    if (this.top == null) this.top = $(this);
    var url = url_arg;
    this.tout = null;
    var pathcheck = /^[0-9a-zA-Z_]+(\/[0-9a-zA-Z_]+)*$/;
    
    this.start = function()
    {
	if (this.tout == null) {
	    this.tout = setTimeout(function() {requestUpdate(upd)}, 1000);
	}
    }

    this.stop = function()
    {
	var tout = this.tout;
	this.tout = null;
	if (tout != null) {
	    clearTimeout(tout);
	}
    }
   
    function requestUpdate(upd) 
    {
	jQuery.getJSON(url, handleValueReply).error(function() {requestError(upd)}).complete(function() {requestUpdateDone(upd)});
    }

    function requestUpdateDone(upd) 
    {
	upd.tout = setTimeout(function() {requestUpdate(upd)}, 1000);
    }
    
    function prepare_input_element(element)
    {
	
	var path = element.getAttribute("path");
	if (!pathcheck.test(path)) {
	    alert("Invalid path '"+path+"'");
	}
	var parts = path.split("/");
	element.getValue = "values";
	for (p in parts) {
	    element.getValue += '["'+parts[p]+'"]';
	}
	
	$(element).keypress(
	    function(event) {
		if (event.keyCode == 27) { // ESC
		    this.editing = false;
		} else if (event.keyCode == 13) { // Return
		    if (this.editing) {
			this.editing = false; 
			var v = null;
			var path = this.getAttribute("path");
			var type = typeof(eval(this.getValue));
			console.log("type="+type);
			if (type=="string") {
			    v = this.value;
			} else if (type=="number") {
			    v = parseFloat(this.value);
		    }
			if (v != null) {
			    console.log("Sending "+JSON.stringify(v)+" to "+url+"/"+path);
			    jQuery.ajax({type:"POST", url:url+"/"+path, dataType:"json", contentType:"application/json", data:JSON.stringify(v)});
			}
		    }
		} else {
		    this.editing = true;
		}
	    });
	$(element).focus(function() {
	    this.editing = true;
	});
	$(element).blur(function() {
	    this.editing = false;
	});
    }
    function handleValueReply(data,status,req)
    {
	values = data;
	top.find("input[path]")
	    .each(function(index,element) 
		  {
		      if (!element.editing) {
			  if (element.getValue == null) {
			      prepare_input_element(element);
			  }
			  try {
			      element.value = eval(element.getValue);
			  } catch(e) {}
		      }
		  });
    }

    function requestError(req,status,error)
    {
    }
}
