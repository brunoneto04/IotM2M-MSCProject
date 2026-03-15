import asyncio
from aiocoap import Message, Context, Code
from aiocoap.numbers.types import Type as MessageType
from define_ip import IP_ADDRESS 

async def send_request(protocol, request, operation, expected):
    try:
        print(f"\nSending {operation}")
        print(f"Expected: {expected}")
        response = await protocol.request(request).response
        print(f'Response Type: {response.mtype}')
        print(f'Response Code: {response.code}')
        if response.payload:
            print(f'Response Payload: {response.payload.decode("utf-8")}')
        return response
    except Exception as e:
        print(f'Failed to {operation}:')
        print(e)
        return None
    
async def main():
    protocol = await Context.create_client_context()

######################-POST-##################################
    payload = b'''
    {
        "m2m:cnt": {
            "rn": "cont_water",
            "ri": "cont_water_id",
            "et": "2025-06-30 21:42:59",
            "lbl": ["key1", "key2", "a", "b"]
        }
    }'''
    create_container = Message(
        code=Code.POST,
        uri=f'coap://{IP_ADDRESS}:5683/mn-name/water',
        payload=payload,
        mtype=MessageType.CON,
    )
    create_container.opt.content_format = 50

    payload2 = b'''
    {
        "m2m:cin": {
        "con": "30"
        }
    }'''
    request_post = Message(
        code=Code.POST,
        uri=f'coap://{IP_ADDRESS}:5683/mn-name/water/cont_water',
        payload=payload2, 
        mtype=MessageType.CON,        
    )
    request_post.opt.content_format = 50    

    request_post_Con_Inexistente = Message(
        code=Code.POST,
        uri=f'coap://{IP_ADDRESS}:5683/mn-name/water/cont_juice',
        payload=payload2, 
        mtype=MessageType.CON,        
    )
    request_post_Con_Inexistente.opt.content_format = 50  
######################-GET-##################################
    request_get = Message(mtype=MessageType.CON, code=Code.GET, uri=f'coap://{IP_ADDRESS}:5683/mn-name/water/cont_water/MbwsRXf9?ty=4')       
    request_get_oldest = Message(mtype=MessageType.CON, code=Code.GET, uri=f'coap://{IP_ADDRESS}:5683/mn-name/water/cont_water/ol')
    request_get_latest = Message(mtype=MessageType.CON, code=Code.GET, uri=f'coap://{IP_ADDRESS}:5683/mn-name/water/cont_water/la')
######################-DELETE-##################################
    delete_container = Message(code=Code.DELETE,uri=f'coap://{IP_ADDRESS}:5683/mn-name/water/cont_water', mtype=MessageType.CON) 
######################-SEND REQUEST-##################################
    requests = [
            (request_get_oldest, "GET Oldest", 404),
            (request_get_latest, "GET Latest", 404),
            (request_post, "POST ", 201),
            (request_post_Con_Inexistente, "POST Con  Inexistente", 404),
            (request_post, "POST", 201),
            (request_get_oldest, "GET Oldest",205),
            (request_get_latest, "GET Latest", 205),  
            (delete_container, "DELETE Container", 202),    
            (create_container, "POST Create Container",201),
    ]

    # Modified for loop to unpack all three values
    for request, operation, expected in requests:
        response = await send_request(protocol, request, operation, expected)
        await asyncio.sleep(1)
    
    #response = await send_request(protocol, request_post, "POST ContentInstance")

if __name__ == "__main__":
    asyncio.run(main())