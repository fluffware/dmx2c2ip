
function find_template(top, tclass)
{
console.log(tclass);
    var e = $('.'+tclass, top);
    var template = e.clone();
    e.hide();
    template.removeClass(tclass);
    return template;
}

function DeviceMenu(url, top_elem)
{
    var device_template = find_template(top_elem, 'device-template');

    jQuery.getJSON(url, handleValueReply).error(requestError);

    function requestError(req,status,error)
    {
	alert("Request failed ("+status+","+error+")");
    }

    function render(value,parent,path) 
    {
	if (typeof(value) == "object") {
	    for (name in value) {
		if (typeof(value[name]) == "object") {
		    for (type in value[name]) {
			var c = device_template.clone();
			$('.replace-name', c).replaceWith(device_label(name,type));
			var a = $('a[href]', c);
			a.each(function(i,e) {
			    e.href += '?type='+type+'&id='+name;
			});
			
			parent.append(c);
		    }
		} else {
		    alert("Not a type group");
		}
	    }
	} else {
	    alert("Not a name group");
	}
    }
    function handleValueReply(data,status,req)
    {
	var elem = $('.device-template', top_elem);
	render(data, elem.parent(), '');
    }
}