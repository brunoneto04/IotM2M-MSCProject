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
    request_post = Message(
         mtype=MessageType.CON,
        code=Code.POST,
        uri=f'coap://{IP_ADDRESS}:5683/mn-name/water',
        payload=payload,
    )
    request_post.opt.content_format = 50    

    request_AE_Ine_post = Message(
         mtype=MessageType.CON,
        code=Code.POST,
        uri=f'coap://{IP_ADDRESS}:5683/mn-name/juice',
        payload=payload,
    )
    request_AE_Ine_post.opt.content_format = 50   

######################-GET-##################################
    request_get = Message(code=Code.GET, uri=f'coap://{IP_ADDRESS}:5683/mn-name/water/cont_water',mtype=MessageType.CON)
######################-PUT-##################################
    payload = b'''
    {
        "m2m:cnt": {
            "et": "2025-07-30 21:44:59"
        }
    }'''

    request_put = Message(
        code=Code.PUT,
        uri=f'coap://{IP_ADDRESS}:5683/mn-name/water/cont_water',
        payload=payload,
        mtype=MessageType.CON
    )
    request_put.opt.content_format = 50

    request__AE_ine_put = Message(
        code=Code.PUT,
        uri=f'coap://{IP_ADDRESS}:5683/mn-name/juice/cont_water',
        payload=payload,
        mtype=MessageType.CON
    )
    request__AE_ine_put.opt.content_format = 50

    payload2 = b'''
    {
        "m2m:cnt": {
            "et": "2025-01-30 21:44:59"
        }
    }'''

    request_put_bad_et = Message(
        code=Code.PUT,
        uri=f'coap://{IP_ADDRESS}:5683/mn-name/water/cont_water',
        payload=payload2,
        mtype=MessageType.CON
    )
    request_put_bad_et.opt.content_format = 50

######################-DELETE-##################################
    request_delete = Message(code=Code.DELETE,uri=f'coap://{IP_ADDRESS}:5683/mn-name/water/cont_water', mtype=MessageType.CON)   
######################-SEND REQUEST-##################################
    requests = [
            (request_delete, "DELETE", 202),
            (request_delete, "DELETE_Container_Inexistente", 404),
            (request_get, "GET_Container_Inexistente", 404),
            (request_put, "PUT_Container_Inexistente", 404),
            (request_AE_Ine_post, "POST AE Inexistente", 404),  
            (request_post, "POST", 201),
            (request_post, "POST_Repetido", 422),
            (request_get, "GET", 205),
            (request__AE_ine_put, "PUT AE Inexistente", 404),
            (request_put, "PUT", 205),
            (request_put_bad_et, "PUT_bad_et", 422),
    ]

    # Modified for loop to unpack all three values
    for request, operation, expected in requests:
        response = await send_request(protocol, request, operation, expected)
        await asyncio.sleep(1)

if __name__ == "__main__":
    asyncio.run(main())