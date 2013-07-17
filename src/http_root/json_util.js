
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

function PrettyPrintJSON(url, res_elem)
{
    jQuery.getJSON(url, handleValueReply).error(requestError);
    function handleValueReply(data,status,req)
    {
	res_elem.text(JSON.stringify(data,undefined, 4));
    }
    function postError(req,status,error)
    {
	alert("Request failed ("+status+","+error+")");
    }
}

function JSONList(url, top_elem)
{
    var elem = $('.container-template', top_elem);
    var container = elem.clone();
    container.removeClass('container-template');
    elem.hide();
    
    elem = $('.leaf-template', top_elem);
    var leaf = elem.clone();
    leaf.removeClass('leaf-template');
    elem.hide();

    jQuery.getJSON(url, handleValueReply).error(requestError);

    function render(value,parent,path) 
    {
	if (typeof(value) == "object") {
	    for (m in value) {
		if (typeof(value[m]) == "object") {
		    var c = container.clone();
		    $('.replace-name', c).replaceWith(m);
		    $('.replace-child', c)
			.each(function () {
			    $(this).replaceWith(render(value[m],
						       $(this).parent(),
						      path+m+"/"));
		    });
		    parent.append(c);
		} else {
		    var l = leaf.clone();
		    $('.replace-name', l).replaceWith(m);
		    $('.replace-value',l).replaceWith(JSON.stringify(value[m]));
		    var inp = $('.value-input', l);
		    inp.attr("path", path+m);
		    parent.append(l);
		}
	    }
	}
    }
    function handleValueReply(data,status,req)
    {
	
	var elem = $('.container-template', top_elem);
	$(':visible', elem.parent()).remove();
	render(data, elem.parent(), '');
    }
    function postError(req,status,error)
    {
	alert("Request failed ("+status+","+error+")");
    }
}



function FieldUpdate(url_arg, top_arg)
{
    var values;
    var top = top_arg;
    var url = url_arg;
    var tout = null;
    var pathcheck = /^[0-9a-zA-Z_]+(\/[0-9a-zA-Z_]+)*$/;
    
    this.start = function()
    {
	if (tout == null) {
	    tout = setTimeout(requestUpdate, 1000);
	}
    }

    this.stop = function()
    {
	var t = tout;
	tout = null;
	if (t != null) {
	    clearTimeout(t);
	}
    }
   
    function requestUpdate() 
    {
	jQuery.getJSON(url, handleValueReply).error(requestError).complete(requestUpdateDone);
    }

    function requestUpdateDone() 
    {
	tout = setTimeout(requestUpdate, 1000);
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
			if (type=="string") {
			    v = this.value;
			} else if (type=="number") {
			    v = parseFloat(this.value);
		    }
			if (v != null) {
			    //console.log("Sending "+JSON.stringify(v)+" to "+url+"/"+path);
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
	top.find("input[path]:visible")
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
